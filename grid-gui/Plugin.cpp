// ======================================================================
/*!
 * \brief SmartMet Grid Gui plugin implementation
 */
// ======================================================================

#include "Plugin.h"
#include "Attributes.h"

#include <grid-files/common/GeneralFunctions.h>
#include <grid-files/common/ImagePaint.h>
#include <grid-files/identification/GridDef.h>
#include <grid-files/map/Topography.h>
#include <grid-files/common/ShowFunction.h>
#include <spine/SmartMet.h>
#include <macgyver/Hash.h>
#include <macgyver/TimeFormatter.h>
#include <macgyver/FastMath.h>
#include <algorithm>
#include <boost/bind/bind.hpp>
#include <macgyver/DateTime.h>
#include <boost/lexical_cast.hpp>
#include <webp/encode.h>
#include <webp/mux.h>
#include <optional>

#define FUNCTION_TRACE FUNCTION_TRACE_OFF


namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{

using namespace SmartMet::Spine;

namespace
{
// Request values are copied into the session string, which page_main() writes unescaped
// into HTML attributes and JavaScript string literals (onchange="getPage(...,'/grid-gui?
// session=...')"). Reject any value that could end those contexts or start markup, so a
// crafted link cannot inject script. Legitimate values (producer, parameter and colour map
// names, times, numbers) never contain these characters.
bool isSafeRequestValue(const std::string& value)
{
  for (unsigned char ch : value)
  {
    if (ch < 0x20 || ch == 0x7f)
      return false;
    switch (ch)
    {
      case '<':
      case '>':
      case '"':
      case '\'':
      case '`':
      case '\\':
      case '&':
        return false;
      default:
        break;
    }
  }
  return true;
}
}  // namespace



// ----------------------------------------------------------------------
/*!
 * \brief Plugin constructor
 */
// ----------------------------------------------------------------------

/*! \brief GridGui: Constructor. */

Plugin::Plugin(Spine::Reactor *theReactor, const char *theConfig)
    :  itsModuleName("GridGui")
{
  using namespace boost::placeholders;
  try
  {
    const char *configAttribute[] =
    {
        "smartmet.plugin.grid-gui.grid-files.configFile",
        "smartmet.plugin.grid-gui.colorMapFiles",
        "smartmet.plugin.grid-gui.colorFile",
        "smartmet.plugin.grid-gui.animationEnabled",
        "smartmet.plugin.grid-gui.imageCache.directory",
        "smartmet.plugin.grid-gui.imageCache.maxImages",
        "smartmet.plugin.grid-gui.imageCache.minImages",
        nullptr
    };

    itsReactor = theReactor;
    itsColors_lastModified = 0;
    itsImageCache_dir = "/tmp";
    itsImageCache_maxImages = 1000;
    itsImageCache_minImages = 500;
    itsAnimationEnabled = true;
    itsImageCounter = 0;
    itsProducerFile_modificationTime = 0;

    if (theReactor->getRequiredAPIVersion() != SMARTMET_API_VERSION)
      throw Fmi::Exception(BCP, "GridGui plugin and Server API version mismatch");

    // Register the handler
    if (!theReactor->addPrivateContentHandler(
            this, "/grid-gui", boost::bind(&Plugin::callRequestHandler, this, _1, _2, _3)))
      throw Fmi::Exception(BCP, "Failed to register GridGui request handler");


    itsConfigurationFile.readFile(theConfig);

    // The new user interface: its pages, files and JSON API are below one URL prefix

    itsUiUrl = "/gui";
    itsUiDirectory = "/usr/share/smartmet/grid-gui";
    if (itsConfigurationFile.findAttribute("smartmet.plugin.grid-gui.ui.url"))
      itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.ui.url",itsUiUrl);
    if (itsConfigurationFile.findAttribute("smartmet.plugin.grid-gui.ui.directory"))
      itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.ui.directory",itsUiDirectory);

    // The user interface is private like /grid-gui: it is not in the URI list of the server,
    // so frontends do not route it, and it accepts only local and private network addresses
    // unless ip_filters says otherwise. Its queries can be arbitrarily large, so it must not be
    // offered to the public. The ui.visible setting of earlier versions is refused rather than
    // ignored, so that nobody relies on it.
    if (itsConfigurationFile.findAttribute("smartmet.plugin.grid-gui.ui.visible"))
      throw Fmi::Exception(BCP, "ui.visible is no longer supported: the user interface is always private");

    if (!itsUiUrl.empty())
    {
      if (itsUiUrl[0] != '/' || itsUiUrl == "/grid-gui")
        throw Fmi::Exception(BCP, "ui.url must start with '/' and differ from /grid-gui").addParameter("ui.url",itsUiUrl);

      auto handler = boost::bind(&Plugin::callRequestHandler, this, _1, _2, _3);
      if (!theReactor->addPrivateContentHandler(this, itsUiUrl, handler, {}, true))
        throw Fmi::Exception(BCP, "Failed to register GridGui user interface handler");
    }

    uint t=0;
    while (configAttribute[t] != nullptr)
    {
      if (!itsConfigurationFile.findAttribute(configAttribute[t]))
      {
        Fmi::Exception exception(BCP, "Missing configuration attribute!");
        exception.addParameter("File",theConfig);
        exception.addParameter("Attribute",configAttribute[t]);
        throw exception;
      }
      t++;
    }

    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.grid-files.configFile",itsGridConfigFile);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.colorMapFiles",itsColorMapFileNames);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.colorFile",itsColorFile);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.animationEnabled",itsAnimationEnabled);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.imageCache.directory",itsImageCache_dir);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.imageCache.maxImages",itsImageCache_maxImages);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.imageCache.minImages",itsImageCache_minImages);


    std::vector<std::string> projVec;
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.blockedProjections",projVec);
    for (auto& it : projVec)
      itsBlockedProjections.insert(std::stoi(it));

    // The grid definitions (Identification::gridDef) are shared by the whole
    // process and are initialized by the grid engine from its own
    // grid-files.configFile; init() waits for the engine. Initializing them here
    // as well only raced with the engine: whichever call came first won. The
    // file configured here is used for the topography.
    Map::topography.init(itsGridConfigFile.c_str(),true,true,true);

    for (auto& itsColorMapFileName : itsColorMapFileNames)
    {
      T::ColorMapFile colorMapFile;
      colorMapFile.init(itsColorMapFileName);
      itsColorMapFiles.emplace_back(colorMapFile);
    }

    loadColorFile();
    loadProducerFile();


    // Removing files from the image cache.

    std::vector<std::string> filePatterns;
    std::set<std::string> dirList;
    std::vector<std::pair<std::string,std::string>> fileList;

    filePatterns.emplace_back("grid-gui-image_*");

    getFileList(itsImageCache_dir.c_str(),filePatterns,false,dirList,fileList);

    for (auto& it : fileList)
    {
      std::string fname = itsImageCache_dir + "/" + it.second;
      (void)remove(fname.c_str());
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Constructor failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Destructor
 */
// ----------------------------------------------------------------------

/*! \brief GridGui: Destructor. */

Plugin::~Plugin() = default;




// ----------------------------------------------------------------------
/*!
 * \brief Initializator, in this case trivial
 */
// ----------------------------------------------------------------------
/*! \brief GridGui: Init. */

void Plugin::init()
{
  FUNCTION_TRACE
  try
  {
    itsGridEngine = itsReactor->getEngine<Engine::Grid::Engine>("grid", nullptr);
    itsProducerFile = itsGridEngine->getProducerFileName();
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}





// ----------------------------------------------------------------------
/*!
 * \brief Shutdown the plugin
 */
// ----------------------------------------------------------------------

/*! \brief GridGui: Shutdown. */

void Plugin::shutdown()
{
  FUNCTION_TRACE
  try
  {
    std::cout << "  -- Shutdown requested (grid-plugin)\n";
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}





namespace
{
/*! \brief Splits the line in place into fields separated by ';' or newline, ignoring
    separators inside quotes. Returns the number of field pointers set (at most 100),
    which includes the field starting after the last separator. */

uint splitFields(char *st,char *field[100])
{
  bool ind = false;
  uint c = 1;
  field[0] = st;
  char *p = st;
  while (*p != '\0'  &&  c < 100)
  {
    if (*p == '"')
      ind = !ind;

    if ((*p == ';'  || *p == '\n') && !ind)
    {
      *p = '\0';
      p++;
      field[c] = p;
      c++;
    }
    else
    {
      p++;
    }
  }
  return c;
}
}  // namespace

/*! \brief GridGui: Load producer file. */

void Plugin::loadProducerFile()
{
  FUNCTION_TRACE
  try
  {
    if (itsProducerFile_modificationTime == getFileModificationTime(itsProducerFile.c_str()))
      return;

    AutoThreadLock lock(&itsThreadLock);
    FILE* file = fopen(itsProducerFile.c_str(), "re");
    if (file == nullptr)
    {
      Fmi::Exception exception(BCP, "Cannot open the producer file!");
      exception.addParameter("Filename", itsProducerFile);
      throw exception;
    }

    itsProducerList.clear();

    char st[1000];

    while (fgets(st, 1000, file) != nullptr)
    {
      if (st[0] != '#')
      {
        char* field[100];
        uint c = splitFields(st,field);
        c--;

        std::string key;
        for (uint t=0; t<c; t++)
        {
          if (field[t][0] != '\0')
          {
            itsProducerList.insert(toUpperString(field[t]));
          }
        }
      }
    }
    (void)fclose(file);

    itsProducerFile_modificationTime = getFileModificationTime(itsProducerFile.c_str());
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}




/*! \brief GridGui: Get color map file. */

T::ColorMapFile* Plugin::getColorMapFile(const std::string& colorMapName)
{
  FUNCTION_TRACE
  try
  {
    T::ColorMapFile *map = nullptr;
    for (auto& itsColorMapFile : itsColorMapFiles)
    {
      itsColorMapFile.checkUpdates();
      if (itsColorMapFile.hasName(colorMapName.c_str()))
        map = &itsColorMapFile;
    }
    return map;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: Load color file. */

void Plugin::loadColorFile()
{
  FUNCTION_TRACE
  try
  {
    FILE *file = fopen(itsColorFile.c_str(),"re");
    if (file == nullptr)
    {
      Fmi::Exception exception(BCP,"Cannot open file!");
      exception.addParameter("Filename",itsColorFile);
      throw exception;
    }

    itsColors.clear();

    char st[1000];

    while (fgets(st,1000,file) != nullptr)
    {
      if (st[0] != '#')
      {
        char *field[100];
        uint c = splitFields(st,field);

        if (c > 1)
        {
          if (field[0][0] != '\0'  &&  field[1][0] != '\0')
          {
            std::string colorName = field[0];
            uint color = 0x00FFFFFF;
            if (strlen(field[1]) == 8)
              color = strtoul(field[1],nullptr,16);
            else  // No alpha
              color = 0xFF000000 | (strtoul(field[1],nullptr,16) & 0x00FFFFFF);

            itsColors.emplace_back(colorName,color);
          }
        }
      }
    }
    (void)fclose(file);

    itsColors_lastModified = getFileModificationTime(itsColorFile.c_str());
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: Get color value. */

uint Plugin::getColorValue(const std::string& colorName)
{
  FUNCTION_TRACE
  try
  {
    for (auto& itsColor : itsColors)
    {
      if (itsColor.first == colorName)
        return itsColor.second;
    }

    return 0x00FFFFFF;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Scan a parameter-value vector and derive a min/max/step triple for
 *  the implicit colour scaling used by saveMap() when no colour map file is configured. */

void Plugin::computeValueRangeForMap(const T::ParamValue_vec& values,
                                     double& minValue,
                                     double& maxValue,
                                     double& step)
{
  double total = 0;
  uint cnt = 0;
  uint sz = values.size();

  for (uint t = 0; t < sz; t++)
  {
    double val = values[t];
    if (val != ParamValueMissing)
    {
      total = total + val;
      cnt++;
      minValue = std::min(val, minValue);
      maxValue = std::max(val, maxValue);
    }
  }
  double avg = total / (double)cnt;
  double dd = maxValue - minValue;
  double ddd = avg - minValue;
  step = dd / 200;
  if (maxValue > (minValue + 5*ddd))
    step = 5*ddd / 200;
}




namespace
{

// Colour of a map value: from the colour map, or a hue ramp over minValue..

uint mapValueColor(T::ColorMapFile *colorMapFile,T::ParamValue val,double minValue,double step,unsigned char hue,unsigned char saturation,unsigned char blur)
{
  uint col = 0xFFFFFFFF;
  if (colorMapFile)
  {
    col = colorMapFile->getSmoothColor(val);
  }
  else
  {
    uint vv = ((val - minValue) / step);
    uint v = 200 - vv;
    if (vv > 200)
      v = 0;

    v = v / blur;
    v = v * blur;
    v = v + 55;
    col = 0xFF000000 + hsv_to_rgb(hue,saturation,C_UCHAR(v));
  }
  return col;
}



// Paint the land border: the first land pixel of a row or a column gets the
// border colour, and a sea pixel recolours its land neighbours on the left
// and above.

void paintLandBorder(uint *image,int width,int x,int y,bool land,bool prevLand,bool landAbove,uint landBorder,uint coordinateLines,uint& col,uint& lbcol)
{
  if (land & (!prevLand || !landAbove))
  {
    col = landBorder;
    lbcol = col;
  }

  if (!land)
  {
    if (prevLand  &&  x > 0  &&  image[y*width + x-1] != coordinateLines)
      image[y*width + x-1] = lbcol;

    if (landAbove &&  y > 0  && image[(y-1)*width + x] != coordinateLines)
      image[(y-1)*width + x] = lbcol;
  }
}



// Does the missing value option say that zeros are missing values?

bool isZeroMissing(const std::string& missingStr)
{
  return !missingStr.empty() &&  strcasecmp(missingStr.c_str(),"Zero") == 0;
}



// The value of a map pixel, a zero is missing when zeroIsMissingValue is set

T::ParamValue mapValue(T::ParamValue val,bool zeroIsMissingValue)
{
  if (val == 0.0   &&  zeroIsMissingValue)
    return ParamValueMissing;
  return val;
}



// Is pixel (x,y) on a drawn coordinate line (every xx'th column, every yy'th row)?

bool onCoordinateLine(uint coordinateLines,int x,int y,uint xx,uint yy)
{
  return (coordinateLines & 0xFF000000) && ((x % xx) == 0  ||  (y % yy) == 0);
}

// The same for a map of a part of the world: a line where a pixel reaches a multiple of
// 10 degrees, and lines 1 degree apart when the area is small

bool onAreaCoordinateLine(uint coordinateLines,int x,int y,const MapArea& area)
{
  if (!(coordinateLines & 0xFF000000))
    return false;
  const double spacing = (area.dlon < 0.01 ? 1.0 : 10.0);
  auto crosses = [spacing](double a,double b) { return std::floor(a / spacing) != std::floor(b / spacing); };
  const double lonLeft = area.lon0 + x * area.dlon;
  const double latTop = area.lat1 - y * area.dlat;
  return crosses(lonLeft,lonLeft + area.dlon) || crosses(latTop - area.dlat,latTop);
}

// Is the pixel on a drawn coordinate line of the whole world or of a part of it?

bool onMapCoordinateLine(uint coordinateLines,int x,int y,uint xx,uint yy,const MapArea* area)
{
  if (area == nullptr)
    return onCoordinateLine(coordinateLines,x,y,xx,yy);
  return onAreaCoordinateLine(coordinateLines,x,y,*area);
}

// The longitude and latitude of a map pixel: of the whole world, or the pixel centre of a part of it

void mapPixelLonLat(int x,int y,int width,int height,const MapArea* area,double& xc,double& yc)
{
  if (area != nullptr)
  {
    xc = area->lon0 + (x + 0.5) * area->dlon;
    yc = area->lat1 - (y + 0.5) * area->dlat;
    return;
  }
  auto dWidth = C_DOUBLE(width);
  auto dHeight = C_DOUBLE(height);
  double xd = 360/dWidth;
  double yd = 180/dHeight;
  xc = xd*(x-(dWidth/2));
  yc = yd*((dHeight-y-1)-(dHeight/2));
}

}  // namespace



/*! \brief GridGui: Save map. */

void Plugin::saveMap(const char *imageFile,uint columns,uint rows,T::ParamValue_vec&  values,unsigned char hue,unsigned char saturation,unsigned char blur,uint coordinateLines,uint landBorder,const std::string& landMask,const std::string& seaMask,const std::string& colorMapName,const std::string& missingStr,const MapArea* area)
{
  FUNCTION_TRACE
  try
  {
    const bool zeroIsMissingValue = isZeroMissing(missingStr);

    uint landColor = getColorValue(landMask);
    uint seaColor = getColorValue(seaMask);

    T::ColorMapFile *colorMapFile = nullptr;

    if (!colorMapName.empty() &&  strcasecmp(colorMapName.c_str(),"None") != 0)
      colorMapFile = getColorMapFile(colorMapName);

    uint sz = values.size();

    if (sz == 0)
      return;

    if (sz != (columns * rows))
    {
      // A malformed/mismatched grid must not terminate the whole server process.
      throw Fmi::Exception(BCP, "The number of values does not match the grid size")
          .addParameter("values", Fmi::to_string(sz))
          .addParameter("columns", Fmi::to_string(columns))
          .addParameter("rows", Fmi::to_string(rows));
    }

    double maxValue = -1000000000;
    double minValue = 1000000000;
    double step = 0;

    if (!colorMapFile)
      computeValueRangeForMap(values, minValue, maxValue, step);

    int width = columns;
    int height = rows;

    uint xx = columns / 36;
    uint yy = rows / 18;

    uint *image = new uint[width*height];

    uint c = 0;
    // Heap-allocated rather than a stack VLA: width comes from the grid geometry and a
    // large grid would otherwise overflow the stack.
    std::vector<bool> yLand(width, false);

    ModificationLock *modificationLock = nullptr;
    if (colorMapFile != nullptr)
      modificationLock = colorMapFile->getModificationLock();

    uint lbcol = landBorder;

    AutoReadLock lock(modificationLock);

    for (int y=0; y<height; y++)
    {
      bool prevLand = false;
      for (int x=0; x<width; x++)
      {
        T::ParamValue val = mapValue(values[c],zeroIsMissingValue);

        uint col = mapValueColor(colorMapFile,val,minValue,step,hue,saturation,blur);

        double xc = 0;
        double yc = 0;
        mapPixelLonLat(x,y,width,height,area,xc,yc);

        bool land = Map::topography.isLand(xc,yc);

        if (val == ParamValueMissing || ((col & 0xFF000000) == 0))
          col = land ? landColor : seaColor;

        if (landBorder & 0xFF000000)
          paintLandBorder(image,width,x,y,land,prevLand,yLand[x],landBorder,coordinateLines,col,lbcol);

        if (onMapCoordinateLine(coordinateLines,x,y,xx,yy,area))
          col = coordinateLines;

        yLand[x] = land;
        prevLand = land;
        image[y*width + x] = col;
        c++;
      }
    }

    //jpeg_save(imageFile,image,height,width,100);

    png_save(imageFile,image,width,height,1);

    delete[] image;

    checkImageCache();
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Check image cache. */

void Plugin::checkImageCache()
{
  FUNCTION_TRACE
  try
  {
    AutoThreadLock lock(&itsThreadLock);

    uint cnt = itsImages.size();

    if (cnt > itsImageCache_maxImages)
    {
      std::map<std::string,std::string> tmpImages;

      for (auto& itsImage : itsImages)
      {
        tmpImages.insert(std::pair<std::string,std::string>(itsImage.second,itsImage.first));
      }

      for (auto it = tmpImages.begin(); it != tmpImages.end()  &&  cnt > itsImageCache_minImages; ++it)
      {
        (void)remove(it->first.c_str());
        itsImages.erase(it->second);
        cnt--;
      }
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Save image. */

namespace
{
/*! \brief The values of one field in its own geometry, or interpolated to the geometry
    geomId when it differs from the field's geometry. */

bool getFieldValues(DataServer::ServiceInterface& dataServer,T::FileId fileId,T::MessageIndex messageIndex,T::GeometryId fieldGeomId,T::GeometryId geomId,uint& columns,uint& rows,T::ParamValue_vec& values)
{
  if (geomId == fieldGeomId)
  {
    T::GridData gridData;
    int result = dataServer.getGridData(0,fileId,messageIndex,gridData);
    if (result != 0)
    {
      Fmi::Exception exception(BCP,"Data fetching failed!");
      exception.addParameter("Result",DataServer::getResultString(result));
      throw exception;
    }
    columns = gridData.mColumns;
    rows = gridData.mRows;
    values = std::move(gridData.mValues);
    return true;
  }

  if (!Identification::gridDef.getGridDimensionsByGeometryId(geomId,columns,rows) || fileId == 0)
    return false;

  T::AttributeList attributeList;
  attributeList.addAttribute("grid.geometryId",std::to_string(geomId));
  attributeList.addAttribute("grid.areaInterpolationMethod",std::to_string(T::AreaInterpolationMethod::Linear));
  double_vec modificationParameters;
  int result = dataServer.getGridValueVectorByGeometry(0,fileId,messageIndex,attributeList,0,modificationParameters,values);
  if (result != 0)
    throw Fmi::Exception(BCP,"Data fetching failed!");
  return true;
}
}  // namespace



bool Plugin::getImageValues(const ImagePaintParameters& params,T::GeometryId& geometryId,uint& columns,uint& rows,T::ParamValue_vec& values)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    geometryId = params.geometryId;
    if (params.projectionId > 0  &&  params.projectionId != params.geometryId)
      geometryId = params.projectionId;

    if (!getFieldValues(*dataServer,params.fileId,params.messageIndex,params.geometryId,geometryId,columns,rows,values))
      return false;

    if (params.fileId2 == 0)
      return true;

    // A difference image: the second field minus the first one. The fields are of the
    // same parameter and geometry, so their grids are identical.

    uint columns2 = 0;
    uint rows2 = 0;
    T::ParamValue_vec values2;
    if (!getFieldValues(*dataServer,params.fileId2,params.messageIndex2,params.geometryId,geometryId,columns2,rows2,values2))
      return false;

    if (columns2 != columns || rows2 != rows || values2.size() != values.size())
      throw Fmi::Exception(BCP,"The fields of a difference image have different grids");

    for (std::size_t t = 0; t < values.size(); t++)
    {
      if (values[t] != ParamValueMissing && values2[t] != ParamValueMissing)
        values[t] = values2[t] - values[t];
      else
        values[t] = ParamValueMissing;
    }
    return true;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("File",std::to_string(params.fileId));
    exception.addParameter("Message",std::to_string(params.messageIndex));
    throw exception;
  }
}



void Plugin::saveImage(ImagePaintParameters& params)
{
  FUNCTION_TRACE
  try
  {
    T::GeometryId geomId = params.geometryId;
    if (params.projectionId > 0  &&  params.projectionId != params.geometryId)
      geomId = params.projectionId;

    T::Coordinate_vec emptyCoordinates;
    T::Coordinate_vec *coordinates = &emptyCoordinates;
    T::Coordinate_vec *lineCoordinates = &emptyCoordinates;

    T::Coordinate_svec coordinatesPtr;
    if (geomId != 0)
    {
      coordinatesPtr = Identification::gridDef.getGridLatLonCoordinatesByGeometryId(geomId);
      if (coordinatesPtr)
        coordinates = coordinatesPtr.get();
    }


    T::Coordinate_svec lineCoordinatesPtr;
    if (params.coordinateLine_color & 0xFF000000)
    {
      if (geomId != 0)
      {
        lineCoordinatesPtr = Identification::gridDef.getGridLatLonCoordinateLinePointsByGeometryId(geomId);
        if (lineCoordinatesPtr)
          lineCoordinates = lineCoordinatesPtr.get();
      }
    }

    uint cols = 0;
    uint rows = 0;
    T::ParamValue_vec values;
    if (getImageValues(params,geomId,cols,rows,values))
    {
      params.geometryId = geomId;
      saveImage(params,cols,rows,values,*coordinates,lineCoordinates);
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




HsvScale hsvScale(const T::ParamValue_vec& values)
{
  HsvScale scale;
  double maxValue = -1000000000;
  double minValue = 1000000000;
  double total = 0;
  uint cnt = 0;

  for (double val : values)
  {
    if (val != ParamValueMissing)
    {
      total = total + val;
      cnt++;
      minValue = std::min(val, minValue);
      maxValue = std::max(val, maxValue);
    }
  }

  double avg = total / (double)cnt;
  double dd = maxValue - minValue;
  double ddd = avg-minValue;
  double step = dd / 200;
  if (maxValue > (minValue + 5*ddd))
    step = 5*ddd / 200;

  scale.minValue = minValue;
  scale.maxValue = maxValue;
  scale.avgValue = avg;
  scale.step = step;
  scale.count = cnt;
  return scale;
}



double differenceRange(const T::ParamValue_vec& values)
{
  std::vector<double> diffs;
  diffs.reserve(values.size());
  for (double val : values)
  {
    if (val != ParamValueMissing)
      diffs.push_back(std::abs(val));
  }

  if (diffs.empty())
    return 1;

  auto pos = diffs.begin() + static_cast<std::ptrdiff_t>((diffs.size() - 1) * 0.99);
  std::nth_element(diffs.begin(),pos,diffs.end());
  if (*pos > 0)
    return *pos;

  // Nearly all values are equal: use the largest difference, or 1 if there is none
  double maxDiff = *std::max_element(diffs.begin(),diffs.end());
  return (maxDiff > 0 ? maxDiff : 1);
}



uint differenceColor(double value,double range)
{
  // ColorBrewer RdBu end points: 0x2166AC (negative) and 0xB2182B (positive)
  double p = (range > 0 ? value / range : 0);
  p = std::max(-1.0, std::min(1.0, p));

  uint r = 0xB2;
  uint g = 0x18;
  uint b = 0x2B;
  if (p < 0)
  {
    r = 0x21;
    g = 0x66;
    b = 0xAC;
    p = -p;
  }

  auto mix = [p](uint c) { return C_UINT(std::lround(255 - (255 - C_INT(c)) * p)); };
  return 0xFF000000 | (mix(r) << 16) | (mix(g) << 8) | mix(b);
}



namespace
{

// Shading pixel: black with alpha shadow * -m for m < 0, white with alpha light * m otherwise

uint shadingPixel(double m,uint shadow,uint light)
{
  if (m < 0)
  {
    uint pp = (uint)(-(double)shadow*m);
    pp = std::min<SmartMet::uint>(pp, 255);

    return pp << 24;
  }
  uint pp = (uint)((double)light*m);
  pp = std::min<SmartMet::uint>(pp, 255);

  return (pp << 24) + 0xFFFFFF;
}



// Is the image size usable, and are there enough values for it?

bool validImageSize(int width,int height,std::size_t sz)
{
  if (width <= 0 || height <= 0)
    return false;

  uint size = width*height;

  if (size == 0 || size > 100000000)
    return false;

  if (sz < size)
  {
    printf("ERROR: There are not enough values (= %zu) for the grid (%d x %d)!\n",sz,width,height);
    return false;
  }
  return true;
}



// Sea colour and sea shading layers of the image

void buildSeaImages(const ImagePaintParameters& params,int width,int height,uint size,const T::Coordinate_vec& coordinates,bool rotate,uint *&seaShadingImage,uint *&seaImage)
{
  std::vector<float> seaShadings;
  seaShadingImage = new uint[size];
  seaImage = new uint[size];
  uint c = 0;
  for (int y=0; y<height; y++)
  {
    int yy = y;
    if (rotate)
      yy = height-y-1;

    for (int x=0; x<width; x++)
    {
      uint pixpos = yy*width + x;
      seaShadingImage[pixpos] = 0x000000;
      seaImage[pixpos] = 0x00000000;

      double lon = coordinates[c].x();
      double lat = coordinates[c].y();
      bool land = Map::topography.isLand(lon,lat);


      if (!land)
      {
        if (params.seaColor_position)
          seaImage[pixpos] = params.seaColor;

        if (params.seaShading_position)
        {
          seaShadingImage[pixpos] = shadingPixel(Map::topography.getSeaShading(lon,lat),params.seaShading_shadow,params.seaShading_light);
        }
      }
      c++;
    }
  }
}



// A sea pixel marks its land neighbours on the left and above as land border

void markSeaSideBorder(uint *landBorderImage,int width,int x,int yy,bool prevLand,bool landAbove,uint borderColor)
{
  if (prevLand  &&  x > 0)
  {
    int ppos = yy*width + (x-1);
    landBorderImage[ppos] = borderColor;
  }

  if (landAbove &&  yy > 0)
  {
    int ppos = (yy-1)*width + x;
    landBorderImage[ppos] = borderColor;
  }
}



// Land colour, land shading and land border layers of the image

void buildLandImages(const ImagePaintParameters& params,int width,int height,uint size,const T::Coordinate_vec& coordinates,bool rotate,uint *&landShadingImage,uint *&landImage,uint *&landBorderImage)
{
  // Counting land topography.

  // Heap-allocated rather than a stack VLA (width comes from the grid geometry).
  std::vector<bool> yLand(width, false);

  landShadingImage = new uint[size];
  landImage = new uint[size];
  landBorderImage = new uint[size];

  uint c = 0;
  for (int y=0; y<height; y++)
  {
    bool prevLand = false;
    int yy = y;
    if (rotate)
      yy = height-y-1;

    for (int x=0; x<width; x++)
    {
      uint pixpos = yy*width + x;
      landShadingImage[pixpos] = 0x000000;
      landImage[pixpos] = 0x00000000;
      landBorderImage[pixpos] = 0x00000000;

      double lon = coordinates[c].x();
      double lat = coordinates[c].y();

      bool land = Map::topography.isLand(lon,lat);
      if (land)
      {
        if (params.landColor_position)
          landImage[pixpos] = params.landColor;

        if (!prevLand || !yLand[x])
          landBorderImage[pixpos] = params.landBorder_color;

        if (params.landShading_position)
        {
          landShadingImage[pixpos] = shadingPixel((double)Map::topography.getLandShading(lon,lat),params.landShading_shadow,params.landShading_light);
        }
      }
      else
        markSeaSideBorder(landBorderImage,width,x,yy,prevLand,yLand[x],params.landBorder_color);
      yLand[x] = land;
      prevLand = land;
      c++;
    }
  }
}



// Value layer of a difference image: blue for negative and red for positive values,
// white at zero, full colour at -range and range

uint *differenceValueImage(const ImagePaintParameters& params,const T::ParamValue_vec& values,int width,int height,uint size,bool rotate,uint alpha)
{
  uint *valImage = new uint[size];

  double range = params.differenceRange;
  if (range <= 0)
    range = differenceRange(values);

  uint c = 0;
  for (int y=0; y<height; y++)
  {
    int yy = y;
    if (rotate)
      yy = height-y-1;

    for (int x=0; x<width; x++)
    {
      uint pixpos = yy*width + x;
      valImage[pixpos] = 0x00000000;
      T::ParamValue val = values[c];
      if (val != ParamValueMissing)
        valImage[pixpos] = alpha + (differenceColor(val,range) & 0x00FFFFFF);
      c++;
    }
  }
  return valImage;
}



// Value layer coloured with a hue ramp over the value range

uint *hsvValueImage(ImagePaintParameters& params,T::ParamValue_vec& values,int width,int height,uint size,bool rotate,uint alpha)
{
  uint *valImage = new uint[size];

  const HsvScale scale = hsvScale(values);
  const double minValue = scale.minValue;
  const double step = scale.step;

  if (params.paint_blur == 0)
    params.paint_blur = 1;


  uint c = 0;
  for (int y=0; y<height; y++)
  {
    int yy = y;
    if (rotate)
      yy = height-y-1;

    for (int x=0; x<width; x++)
    {
      uint pixpos = yy*width + x;
      // codechecker_false_positive [security.ArrayBound] pixpos < size = width * height, the
      // size of the image
      valImage[pixpos] = 0x00000000;

      T::ParamValue val = values[c];

      if (val == 0.0   &&  params.zeroIsMissing)
        val = ParamValueMissing;

      uint vv = ((val - minValue) / step);
      uint v = 200 - vv;
      if (vv > 200)
        v = 0;

      v = v / params.paint_blur;
      v = v * params.paint_blur;
      v = v + 55;

      if (val != ParamValueMissing)
        valImage[pixpos] = alpha + hsv_to_rgb(params.paint_hue,params.paint_saturation,C_UCHAR(v));

      c++;
    }
  }
  return valImage;
}



// Value layer coloured with the colour map, its alpha scaled by the paint alpha

uint *colorMapValueImage(const ImagePaintParameters& params,T::ColorMapFile& colorMapFile,T::ParamValue_vec& values,int width,int height,uint size,bool rotate,uint alpha)
{
  uint *valImage = new uint[size];

  double amp = (double)params.paint_alpha/255.0;


  uint c = 0;
  ModificationLock *modificationLock = colorMapFile.getModificationLock();
  AutoReadLock lock(modificationLock);

  for (int y=0; y<height; y++)
  {
    int yy = y;
    if (rotate)
      yy = height-y-1;

    for (int x=0; x<width; x++)
    {
      uint pixpos = yy*width + x;
      valImage[pixpos] = 0x00000000;

      T::ParamValue val = values[c];
      if (val == 0.0   &&  params.zeroIsMissing)
        val = ParamValueMissing;

      uint col = colorMapFile.getSmoothColor(val);
      if (params.paint_alpha != 255)
      {
        uint current_alpha = col & 0xFF000000;
        uint current_color = col & 0x00FFFFFF;
        if (current_alpha == 0xFF000000)
        {
          col = alpha | current_color;
        }
        else
        {
          uint new_alpha = ((uint)((double)current_alpha *amp)) & 0xFF000000;
          col = new_alpha | current_color;
        }
      }
      valImage[pixpos] = col;
      c++;
    }
  }
  return valImage;
}



// Final colour of pixel t: the layers merged in their configured order

uint composePixel(const ImagePaintParameters& params,uint t,const uint *landImage,const uint *seaImage,const uint *landShadingImage,const uint *seaShadingImage,const uint *valImage,const uint *landBorderImage)
{
  uint col = 0x00000000;
  uint landcol = 0x00000000;
  uint seacol = 0x00000000;
  uint lscol = 0x00000000;
  uint sscol = 0x00000000;
  uint vcol = 0x00000000;
  uint lbcol = 0x00000000;

  if (landImage)
    landcol = landImage[t];

  if (seaImage)
    seacol = seaImage[t];

  if (landShadingImage)
    lscol = landShadingImage[t];

  if (seaShadingImage)
    sscol = seaShadingImage[t];

  if (valImage)
    vcol = valImage[t];

  if (landBorderImage)
    lbcol = landBorderImage[t];

  if ((landcol & 0xFF000000) &&  params.landColor_position == 1)
    col = landcol;

  if ((seacol & 0xFF000000) &&  params.seaColor_position == 1)
    col = seacol;

  if ((lscol & 0xFF000000)  &&  params.landShading_position == 1)
    col = merge_ARGB(lscol,col);

  if ((sscol & 0xFF000000) &&  params.seaShading_position == 1)
    col = merge_ARGB(sscol,col);


  if (vcol & 0xFF000000)
    col = merge_ARGB(vcol,col);


  if ((landcol & 0xFF000000)  &&  params.landColor_position == 2)
    col = merge_ARGB(landcol,col);

  if ((seacol & 0xFF000000)  &&  params.seaColor_position == 2)
    col = merge_ARGB(seacol,col);

  if ((lscol & 0xFF000000)  &&  params.landShading_position == 2)
    col = merge_ARGB(lscol,col);

  if ((sscol & 0xFF000000) &&  params.seaShading_position == 2)
    col = merge_ARGB(sscol,col);

  if (lbcol & 0xFF000000)
    col = merge_ARGB(lbcol,col);

  //if (lbcol & 0xFF000000)
  //  col = merge_ARGB(lbcol,col);

  return col;
}



// Delete a layer image

void deleteImage(uint *&image)
{
  if (image)
  {
    delete [] image;
    image = nullptr;
  }
}



// The stream directions in row order from the north, a new array of size values

float *streamDirections(T::ParamValue_vec& values,int width,int height,uint size,bool rotate)
{
  auto *direction = new float[size];
  if (!rotate)
  {
    uint idx = 0;
    for (int y=0; y<height; y++)
    {
      for (int x=0; x<width; x++)
      {
        // codechecker_false_positive [security.ArrayBound] idx < size = width * height, the
        // size of the array
        direction[idx] = values[idx];
        idx++;
      }
    }
  }
  else
  {
    uint idx = 0;
    for (int y=0; y<height; y++)
    {
      uint idx2 = (height-y-1) * width;
      for (int x=0; x<width; x++)
      {
        // codechecker_false_positive [security.ArrayBound] idx < size = width * height, the
        // size of the array
        direction[idx] = values[idx2];
        idx++;
        idx2++;
      }
    }
  }
  return direction;
}



// Save the stream lines over the final image as a webp animation of lcolors frames

void saveStreamAnimation(const ImagePaintParameters& params,const uint *finalImage,const uint *streamImage,int width,int height,uint size,const uint *color,uint lcolors,std::vector<int>& timeVect)
{
  uint *wimage[lcolors];
  for (uint t=0; t<lcolors; t++)
    wimage[t] = new uint[size];

  uint idx = 0;
  for (int y = 0; y < height; y++)
  {
    for (int x=0; x < width; x++)
    {
      uint streamCol = streamImage[idx];
      for (uint t=0; t<lcolors; t++)
      {
        uint fcol = finalImage[idx];

        if (streamCol != 0)
        {
          uint lcol = color[(streamCol-1+t) % lcolors];
          fcol = merge_ARGB(lcol,fcol);
        }
        // ARGB to RGBA
        uint newCol = (fcol & 0xFF000000) + ((fcol & 0xFF0000) >> 16) + (fcol & 0x00FF00) + ((fcol & 0xFF) << 16);
        wimage[t][idx] = newCol;
      }
      idx++;
    }
  }

  webp_anim_save(params.imageFile.c_str(),wimage,width,height,lcolors,timeVect);

  for (uint t=0; t<lcolors; t++)
  {
    delete [] wimage[t];
    wimage[t] = nullptr;
  }
}



// Draw stream lines over the final image; true if they were saved as an animation

bool drawStreamLines(const ImagePaintParameters& params,T::ParamValue_vec& values,int width,int height,uint size,bool rotate,uint *finalImage)
{
  float *direction = streamDirections(values,width,height,size,rotate);

  //uint *img = finalImage; //imagePaint.getImage();
  uint *streamImage = new uint[size];

  getStreamlineImage(direction,nullptr,streamImage,width,height,params.stream_step,params.stream_step,params.stream_minLength,params.stream_maxLength);

  uint lcolmax = 0xD0;
  uint lcolors = 16;
  uint color[lcolors];

  uint sc = params.stream_color & 0x00FFFFFF;

  double mp = (double)lcolmax / (double)lcolors;
  std::vector<int> timeVect;
  for (uint t=0; t<lcolors; t++)
  {
    uint cc = lcolmax - (uint)(t*mp);
    color[t] = (cc << 24) + sc;
    timeVect.push_back(50);
  }

  if (params.stream_animation)
  {
    saveStreamAnimation(params,finalImage,streamImage,width,height,size,color,lcolors,timeVect);

    delete [] direction;
    direction = nullptr;

    delete [] streamImage;
    streamImage = nullptr;
    return true;
  }
  for (uint t=0; t<size; t++)
  {
    uint streamCol = streamImage[t];
    if (streamCol != 0)
    {
      finalImage[t] = merge_ARGB(color[(streamCol-1) % lcolors],finalImage[t]);
    }
  }
  delete [] direction;
  direction = nullptr;

  delete [] streamImage;
  streamImage = nullptr;
  return false;
}



// The image of the values: a difference image, colors from the color map or HSV colors.
// None for stream lines.

uint *valueImage(ImagePaintParameters& params,T::ColorMapFile *colorMapFile,T::ParamValue_vec& values,int width,int height,uint size,bool rotate,uint alpha)
{
  if (params.stream_step != 0)
    return nullptr;
  if (params.fileId2 != 0)
    return differenceValueImage(params,values,width,height,size,rotate,alpha);
  if (colorMapFile)
    return colorMapValueImage(params,*colorMapFile,values,width,height,size,rotate,alpha);
  // We do not have colormap - using HSV instead
  return hsvValueImage(params,values,width,height,size,rotate,alpha);
}



// Paint the coordinate lines over the image

void paintCoordinateLines(const ImagePaintParameters& params,uint *image,int width,int height,bool rotate,const T::Coordinate_vec *lineCoordinates)
{
  if (!(params.coordinateLine_color & 0xFF000000)  ||  !lineCoordinates  ||  lineCoordinates->empty())
    return;

  ImagePaint imagePaint(image,width,height,0x0,params.coordinateLine_color,0xFFFFFFFF,false,rotate);

  for (auto it = lineCoordinates->begin(); it != lineCoordinates->end(); ++it)
    imagePaint.paintPixel(C_INT(Fmi::floor(it->x())),C_INT(Fmi::floor(it->y())),params.coordinateLine_color);
}



}  // namespace



/*! \brief GridGui: Save image. */

void Plugin::saveImage(ImagePaintParameters& params,
    int width,
    int height,
    T::ParamValue_vec& values,
    T::Coordinate_vec& coordinates,
    T::Coordinate_vec *lineCoordinates)
{
  FUNCTION_TRACE
  try
  {
    T::ColorMapFile *colorMapFile = nullptr;

    if (!params.paint_colorMapName.empty() &&  strcasecmp(params.paint_colorMapName.c_str(),"None") != 0)
      colorMapFile = getColorMapFile(params.paint_colorMapName);

    if (!validImageSize(width,height,values.size()))
      return;

    uint size = width*height;

    bool rotate = true;
    rotate = coordinates.size() > C_UINT(10*width)  &&  coordinates[0].y() < coordinates[10*width].y();


    uint *finalImage = new uint[size];
    uint *valImage = nullptr;
    uint *landShadingImage = nullptr;
    uint *seaShadingImage = nullptr;
    uint *landImage = nullptr;
    uint *landBorderImage = nullptr;
    uint *seaImage = nullptr;

    if (size == coordinates.size() && (params.seaShading_light || params.seaShading_shadow))
      buildSeaImages(params,width,height,size,coordinates,rotate,seaShadingImage,seaImage);


    if (size == coordinates.size() && (params.landShading_light || params.landShading_shadow || (params.landBorder_color & 0xFF000000)))
      buildLandImages(params,width,height,size,coordinates,rotate,landShadingImage,landImage,landBorderImage);

    uint alpha = (uint)params.paint_alpha << 24;

    valImage = valueImage(params,colorMapFile,values,width,height,size,rotate,alpha);

    for (uint t=0; t<size; t++)
      finalImage[t] = composePixel(params,t,landImage,seaImage,landShadingImage,seaShadingImage,valImage,landBorderImage);

    paintCoordinateLines(params,finalImage,width,height,rotate,lineCoordinates);


    deleteImage(landImage);
    deleteImage(seaImage);
    deleteImage(landShadingImage);
    deleteImage(seaShadingImage);
    deleteImage(valImage);
    deleteImage(landBorderImage);


    // Draw stream lines. An animation is saved there and then.
    if (params.stream_step > 0 && drawStreamLines(params,values,width,height,size,rotate,finalImage))
    {
      delete [] finalImage;
      checkImageCache();
      return;
    }


    if (finalImage)
    {
      //jpeg_save(params.imageFile.c_str(),finalImage,height,width,100);
      png_save(params.imageFile.c_str(),finalImage,width,height,1);

      delete [] finalImage;
      finalImage = nullptr;
    }


    checkImageCache();
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}






/*! \brief GridGui: Page info. */

int Plugin::page_info(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request & /*theRequest*/,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);

    std::ostringstream ostr;

    T::ContentInfo contentInfo;
    int result = contentServer->getContentInfo(0,toUInt64(fileIdStr),toUInt32(messageIndexStr),contentInfo);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getContentInfo : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    T::FileInfo fileInfo;
    result = contentServer->getFileInfoById(0,contentInfo.mFileId,fileInfo);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getFileInfoById : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    T::GenerationInfo generationInfo;
    result = contentServer->getGenerationInfoById(0,contentInfo.mGenerationId,generationInfo);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getGenerationInfoById : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    T::ProducerInfo producerInfo;
    result = contentServer->getProducerInfoById(0,contentInfo.mProducerId,producerInfo);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getProducerInfoById : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    T::AttributeList attributeList;
    result = dataServer->getGridAttributeList(0,contentInfo.mFileId,contentInfo.mMessageIndex,attributeList);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getGridAttributeList : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }


    ostr << "<HTML><BODY>\n";
    ostr << "<TABLE border=\"1\" width=\"100%\" style=\"font-size:14; color:#FFFFFF;\">\n";

    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">Producer</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Id</TD><TD>)" << producerInfo.mProducerId << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Name</TD><TD>)" << producerInfo.mName << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Title</TD><TD>)" << producerInfo.mTitle << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Description</TD><TD>)" << producerInfo.mDescription << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">Generation</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Id</TD><TD>)" << generationInfo.mGenerationId << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Name</TD><TD>)" << generationInfo.mName << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Description</TD><TD>)" << generationInfo.mDescription << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Analysis time</TD><TD>)" << generationInfo.mAnalysisTime << "</TD></TR>\n";
    if (generationInfo.mDeletionTime > 0)
      ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Deletion time</TD><TD>)" << utcTimeFromTimeT(generationInfo.mDeletionTime) << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">File</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Id</TD><TD>)" << fileInfo.mFileId << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Server</TD><TD>)" << fileInfo.mServer << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Protocol</TD><TD>)" << C_INT(fileInfo.mProtocol) << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Type</TD><TD>)" << C_INT(fileInfo.mFileType) << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Name</TD><TD>)" << fileInfo.mName << "</TD></TR>\n";
    if (fileInfo.mDeletionTime > 0)
      ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Deletion time</TD><TD>)" << utcTimeFromTimeT(fileInfo.mDeletionTime) << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">Parameter</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Message index</TD><TD>)" << contentInfo.mMessageIndex << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">File Position</TD><TD>)" << contentInfo.mFilePosition << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Size</TD><TD>)" << contentInfo.mMessageSize << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Forecast time</TD><TD>)" << contentInfo.getForecastTime() << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Level</TD><TD>)" << contentInfo.mParameterLevel << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">FMI identifier</TD><TD>)" << contentInfo.mFmiParameterId << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">FMI name</TD><TD>)" << contentInfo.getFmiParameterName() << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">FMI level identifier</TD><TD>)" << toString(contentInfo.mFmiParameterLevelId) << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Forecast type</TD><TD>)" << contentInfo.mForecastType << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Forecast number</TD><TD>)" << contentInfo.mForecastNumber << "</TD></TR>\n";
    ostr << R"(<TR><TD width="180" bgColor="#E0E0E0">Geometry identifier</TD><TD>)" << contentInfo.mGeometryId << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";


    uint len = attributeList.getLength();
    if (len > 0)
    {
      ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">Attributes</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

      for (uint t=0; t<len; t++)
      {
        T::Attribute *attr = attributeList.getAttributeByIndex(t);
        ostr << "<TR><TD>" << attr->mName << " = " << attr->mValue << "</TD></TR>\n";
      }
      ostr << "</TABLE></TD></TR>\n";
    }


    ostr << "</TABLE>\n";

    ostr << "</BODY></HTML>\n";


    theResponse.setContent(std::string(ostr.str()));
    theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





namespace
{
/*! \brief Appends one 16-byte row of the message hex dump. The section counter
    and the section color are carried over from one row to the next. */

void appendMessageByteRow(std::ostringstream& ostr,uint r,const std::vector<uchar>& messageBytes,const std::vector<uint>& messageSections,uint& cnt,uint& scnt,std::string& color)
{
  uint size = messageBytes.size();
  uint ssize = messageSections.size();

  char tmp[2000];
  char tmp2[2000];

  uint a = r*16;
  char *p = tmp;
  char *p2 = tmp2;
  p2 += sprintf(p2,"<TD bgColor=\"#C0C0C0\"></TD>");

  p += sprintf(p,R"(<TR><TD bgColor="#C0C0C0" width="50">%08X</TD>)",a);
  for (uint c=0; c<16; c++)
  {
    if (scnt < ssize  &&  cnt == messageSections[scnt])
    {
      if ((scnt % 2) == 1)
        color = " bgColor=\"E0E0E0\"";
      else
        color = "";

      scnt++;
    }

    if (cnt < size)
    {
      p += sprintf(p,"<TD width=\"20\"%s>%02X</TD>",color.c_str(),messageBytes[cnt]);

      if (isalnum(messageBytes[cnt]))
        p2 += sprintf(p2,"<TD%s>%c</TD>",color.c_str(),messageBytes[cnt]);
      else
        p2 += sprintf(p2,"<TD%s>.</TD>",color.c_str());
    }
    else
    {
      p += sprintf(p,"<TD width=\"20\"%s></TD>",color.c_str());
      p2 += sprintf(p2,"<TD%s>.</TD>",color.c_str());
    }

    cnt++;
  }
  sprintf(p,"%s</TR>\n",tmp2);
  ostr << tmp;
}
}  // namespace

/*! \brief GridGui: Page message. */

int Plugin::page_message(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request & /*theRequest*/,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);

    if (fileIdStr.empty())
      return HTTP::Status::ok;


    std::ostringstream ostr;

    std::vector<uchar> messageBytes;
    std::vector<uint> messageSections;
    int result = dataServer->getGridMessageBytes(0,toUInt64(fileIdStr),toUInt32(messageIndexStr),messageBytes,messageSections);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getGridMessageBytes : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    uint size = messageBytes.size();

    uint rows = size / 16;
    if ((size % 16) != 0)
      rows++;

    rows = std::min<SmartMet::uint>(rows, 1000);

    char tmp[2000];
    char *p = tmp;

    ostr << "<HTML><BODY>\n";
    ostr << "<TABLE border=\"1\" style=\"font-family:Arial; font-size:14; color:#000000; background:#FFFFFF;\">\n";
    ostr << R"(<TR bgColor="#A0A0A0"><TD width="50">Address</TD>)";


    for (uint c=0; c<16; c++)
      p += sprintf(p,R"(<TD width="20" align="center">%02X</TD>)",c);

    p += sprintf(p,"<TD width=\"20\"></TD>");

    for (uint c=0; c<16; c++)
      p += sprintf(p,R"(<TD width="20" align="center">%02X</TD>)",c);

    ostr << tmp;
    ostr << "</TR>";

    std::string color;

    uint cnt = 0;
    uint scnt = 0;
    for (uint r=0; r<rows; r++)
      appendMessageByteRow(ostr,r,messageBytes,messageSections,cnt,scnt,color);

    ostr << "</TABLE>\n";

    ostr << "</BODY></HTML>\n";

    theResponse.setContent(std::string(ostr.str()));
    theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Page download. */

int Plugin::page_download(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request & /*theRequest*/,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);

    std::ostringstream ostr;

    std::vector<uchar> messageBytes;
    std::vector<uint> messageSections;
    int result = dataServer->getGridMessageBytes(0,toUInt64(fileIdStr),toUInt32(messageIndexStr),messageBytes,messageSections);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getGridMessageBytes : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    uint sz = messageBytes.size();
    if (sz > 0)
    {
      auto *content = new std::vector<char>();
      content->reserve(sz);

      std::shared_ptr<std::vector<char>> sContent;
      sContent.reset(content);

      for (uint t=0; t<sz; t++)
      {
        uchar ch = messageBytes[t];
        char *c = (char*)&ch;

        content->emplace_back(*c);
      }

      content->emplace_back('7');
      content->emplace_back('7');
      content->emplace_back('7');
      content->emplace_back('7');

      std::string val = "attachment; filename=message_" + fileIdStr + "_" + messageIndexStr + ".grib";
      theResponse.setHeader("Content-Disposition",val);
      theResponse.setContent(sContent);
    }
    else
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "Message does not exist!\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      theResponse.setContent(std::string(ostr.str()));
    }
    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





namespace
{
/*! \brief Returns the printf format for the grid values based on their maximum. */

std::string valueFormatString(const T::ParamValue_vec& values)
{
  T::ParamValue max = ParamValueMissing;
  uint vcnt = values.size();
  for (uint t=0; t<vcnt; t++)
  {
    if (values[t] != ParamValueMissing  &&  (max == ParamValueMissing ||  values[t] > max))
      max = values[t];
  }

  if (max < 0.00001)
    return "%.14f";
  if (max < 0.001)
    return "%.12f";
  if (max < 0.1)
    return "%.6f";
  return "%.3f";
}

/*! \brief Writes a grid value with the given format, or "Null" if it is missing. */

void appendGridValue(std::ostringstream& ostr,T::ParamValue value,const std::string& formatStr)
{
  if (value != ParamValueMissing)
  {
    char tmp[1000];
    sprintf(tmp,formatStr.c_str(),value);
    ostr << tmp;
  }
  else
  {
    ostr << "Null";
  }
}
}  // namespace

/*! \brief GridGui: Page table. */

int Plugin::page_table(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request & /*theRequest*/,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string geometryIdStr = session.getAttribute(ATTR_GEOMETRY_ID);
    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);

    char tmp[1000];

    if (fileIdStr.empty())
      return HTTP::Status::ok;

    std::ostringstream ostr;

    T::GridData gridData;
    int result = dataServer->getGridData(0,toUInt64(fileIdStr),toUInt32(messageIndexStr),gridData);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridData()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    T::GeometryId geometryId = gridData.mGeometryId;
    if (geometryId == 0)
      geometryId = toInt32(geometryIdStr);


    T::Coordinate_svec coordinates = Identification::gridDef.getGridOriginalCoordinatesByGeometryId(geometryId);

    uint c = 0;
    uint height = gridData.mRows;
    uint width = gridData.mColumns;

    uint sz = width * height;
    if (coordinates->size() != sz)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "Cannot get the grid coordinates\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    width = std::min<SmartMet::uint>(width, 100);

    height = std::min<SmartMet::uint>(height, 100);

    ostr << "<HTML><BODY>\n";
    ostr << "<TABLE border=\"1\" style=\"text-align:right; font-size:10pt;\">\n";


    // ### Column index header:

    ostr << "<TR bgColor=\"#E0E0E0\"><TD></TD><TD></TD>";
    for (uint x=0; x<width; x++)
    {
      ostr << "<TD>" << x << "</TD>";
    }
    ostr << "</TR>\n";


    // ### X coordinate header:

    ostr << "<TR bgColor=\"#D0D0D0\"><TD></TD><TD></TD>";
    for (uint x=0; x<width; x++)
    {
      sprintf(tmp,"%.3f",(*coordinates)[x].x());
      ostr << "<TD>" << tmp << "</TD>";
    }
    ostr << "</TR>\n";


    std::string formatStr = valueFormatString(gridData.mValues);

    // ### Rows:

    for (uint y=0; y<height; y++)
    {
      c = y*gridData.mColumns;

      // ### Row index and Y coordinate:

      sprintf(tmp,"%.3f",(*coordinates)[c].y());
      ostr << "<TR><TD bgColor=\"#E0E0E0\">" << y << "</TD><TD bgColor=\"#D0D0D0\">" << tmp << "</TD>";

      // ### Columns:

      for (uint x=0; x<width; x++)
      {
        ostr << "<TD>";
        if (c < sz)
          appendGridValue(ostr,gridData.mValues[c],formatStr);
        c++;
        ostr << "</TD>";
      }
      ostr << "</TR>\n";
    }
    ostr << "</TABLE>\n";
    ostr << "</BODY></HTML>\n";

    theResponse.setContent(std::string(ostr.str()));
    theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");

    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Page coordinates. */

int Plugin::page_coordinates(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request & /*theRequest*/,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);

    char tmp[1000];

    if (fileIdStr.empty())
      return HTTP::Status::ok;

    std::ostringstream ostr;

    T::GridCoordinates coordinates;
    int result = dataServer->getGridCoordinates(0,toUInt64(fileIdStr),toUInt32(messageIndexStr),T::CoordinateTypeValue::LATLON_COORDINATES,coordinates);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridCoordinates()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }


    uint c = 0;
    uint height = coordinates.mRows;
    uint width = coordinates.mColumns;

    width = std::min<SmartMet::uint>(width, 100);

    height = std::min<SmartMet::uint>(height, 100);

    ostr << "<HTML><BODY>\n";
    ostr << "<TABLE border=\"1\" style=\"text-align:right; font-size:10pt;\">\n";


    // ### Column index header:

    ostr << "<TR bgColor=\"#E0E0E0\"><TD></TD>";
    for (uint x=0; x<width; x++)
    {
      ostr << "<TD>" << x << "</TD>";
    }
    ostr << "</TR>\n";


    // ### Rows:

    for (uint y=0; y<height; y++)
    {
      c = y*coordinates.mColumns;

      // ### Row index and Y coordinate:

      ostr << "<TR><TD bgColor=\"#E0E0E0\">" << y << "</TD>";

      // ### Columns:

      for (uint x=0; x<width; x++)
      {
        ostr << "<TD>";
        if (c < coordinates.mCoordinateList.size())
        {
          sprintf(tmp,"%.8f,%.8f",coordinates.mCoordinateList[c].y(),coordinates.mCoordinateList[c].x());
          ostr << tmp;
        }
        c++;
        ostr << "</TD>";
      }
      ostr << "</TR>\n";
    }
    ostr << "</TABLE>\n";
    ostr << "</BODY></HTML>\n";

    theResponse.setContent(std::string(ostr.str()));
    theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");

    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





namespace
{
/*! \brief Maps a relative position (0..1) to a cell index clamped into 0..count-1. */

int clampedCellIndex(float pos,uint count)
{
  int idx = static_cast<int>(pos * count);
  idx = std::max(idx, 0);
  if (idx >= static_cast<int>(count)) idx = static_cast<int>(count) - 1;
  return idx;
}

/*! \brief Sets the grid value (or "Not available" if it is missing) as the response. */

void setValueContent(HTTP::Response &theResponse,T::ParamValue value)
{
  if (value != ParamValueMissing)
    theResponse.setContent(std::to_string(value));
  else
    theResponse.setContent(std::string("Not available"));

  theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
}
}  // namespace

/*! \brief GridGui: Page value. */

int Plugin::page_value(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request & /*theRequest*/,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    T::FileId fileId = session.getUInt64Attribute(ATTR_FILE_ID);
    T::MessageIndex messageIndex = session.getUIntAttribute(ATTR_MESSAGE_INDEX);
    float xPos = session.getDoubleAttribute(ATTR_X);
    float yPos = session.getDoubleAttribute(ATTR_Y);

    if (fileId == 0)
      return HTTP::Status::ok;

    T::ContentInfo contentInfo;
    if (contentServer->getContentInfo(0,fileId,messageIndex,contentInfo) != 0)
      return HTTP::Status::ok;

    if (contentInfo.mGeometryId == 0)
      return HTTP::Status::ok;

    T::GeometryId projectionId = toInt32(session.getAttribute(ATTR_PROJECTION_ID));

    if (projectionId > 0  &&  projectionId != contentInfo.mGeometryId)
    {
      // Image was rendered using the projection geometry's grid layout.  Map the click
      // to the projection cell using the same row-orientation logic as saveImage(),
      // look up the cell's latlon, and query the data by latlon so interpolation crosses
      // geometries correctly.

      uint cols = 0;
      uint rows = 0;

      if (!Identification::gridDef.getGridDimensionsByGeometryId(projectionId,cols,rows))
        return HTTP::Status::ok;

      T::Coordinate_svec latLonCoordinates = Identification::gridDef.getGridLatLonCoordinatesByGeometryId(projectionId);
      if (!latLonCoordinates  ||  latLonCoordinates->size() < static_cast<std::size_t>(cols) * rows)
        return HTTP::Status::ok;

      const auto& coords = *latLonCoordinates;

      // Same rotate test as Plugin::saveImage(): if rows run south-to-north in the
      // coordinate array, the image is flipped vertically so that north is at the top.
      bool rotate = true;
      rotate = coords.size() > C_UINT(10*cols)  &&  coords[0].y() < coords[10*cols].y();

      int col = clampedCellIndex(xPos,cols);
      int row = clampedCellIndex(yPos,rows);

      int dataRow = rotate ? static_cast<int>(rows) - row - 1 : row;
      std::size_t idx = static_cast<std::size_t>(dataRow) * cols + static_cast<std::size_t>(col);

      T::ParamValue value = 0;
      double_vec modificationParameters;
      const auto& p = coords[idx];
      dataServer->getGridValueByPoint(0,fileId,messageIndex,T::CoordinateTypeValue::LATLON_COORDINATES,p.x(),p.y(),T::AreaInterpolationMethod::Linear,0,modificationParameters,value);

      setValueContent(theResponse,value);
      return HTTP::Status::ok;
    }

    uint cols = 0;
    uint rows = 0;

    if (!Identification::gridDef.getGridDimensionsByGeometryId(contentInfo.mGeometryId,cols,rows))
      return HTTP::Status::ok;

    uint height = rows;
    uint width = cols;

    auto dWidth = C_DOUBLE(width);
    auto dHeight = C_DOUBLE(height);

    bool reverseXDirection = false;
    bool reverseYDirection = false;

    if (!Identification::gridDef.getGridDirectionsByGeometryId(contentInfo.mGeometryId,reverseXDirection,reverseYDirection))
      return HTTP::Status::ok;

    double xx = xPos * dWidth;
    double yy = yPos * dHeight;

    if (!reverseYDirection)
      yy = dHeight - (yPos * dHeight);

    if (reverseXDirection)
      xx = dWidth - (xPos * dWidth);

    T::ParamValue value = 0;
    double_vec modificationParameters;
    dataServer->getGridValueByPoint(0,fileId,messageIndex,T::CoordinateTypeValue::GRID_COORDINATES,xx,yy,T::AreaInterpolationMethod::Nearest,0,modificationParameters,value);

    setValueContent(theResponse,value);

    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Load image. */

bool Plugin::loadImage(const char *fname,Spine::HTTP::Response &theResponse)
{
  FUNCTION_TRACE
  try
  {
    long long sz = getFileSize(fname);
    if (sz > 0)
    {
      char buf[10000];
      auto *content = new std::vector<char>();
      content->reserve(sz);

      std::shared_ptr<std::vector<char>> sContent;
      sContent.reset(content);

      FILE *file = fopen(fname,"re");
      if (file != nullptr)
      {
        int n = 0;
        while ((n = fread(buf,1,10000,file)) > 0)
        {
          for (int t=0; t<n; t++)
          {
            content->emplace_back(buf[t]);
          }
        }
        (void)fclose(file);

        //theResponse.setHeader("Content-Type","image/jpg");
        theResponse.setHeader("Content-Type","image/png");
        theResponse.setContent(sContent);
        return true;
      }
      return false;
    }
    std::ostringstream ostr;
    ostr << "<HTML><BODY>\n";
    ostr << "Image does not exist!\n";
    ostr << "</BODY></HTML>\n";
    theResponse.setContent(std::string(ostr.str()));
    theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
    return false;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Loads the image with the given hash into the response if it has
    already been generated. Returns true if the image was found. */

bool Plugin::loadCachedImage(const std::string& hash,Spine::HTTP::Response &theResponse)
{
  AutoThreadLock lock(&itsThreadLock);
  auto it = itsImages.find(hash);
  if (it == itsImages.end())
    return false;

  loadImage(it->second.c_str(),theResponse);
  return true;
}





/*! \brief GridGui: Tests whether another thread is generating the image with the given hash. */

bool Plugin::isImageUnderConstruction(const std::string& hash)
{
  AutoThreadLock lock(&itsThreadLock);
  bool found = false;
  for (const auto& t : itsImagesUnderConstruction)
  {
    if (t == hash)
    {
      found = true;
    }
  }
  return found;
}





/*! \brief GridGui: Marks the image with the given hash to be under construction. Returns
    the slot index which must be cleared when the construction ends. */

uint Plugin::startImageConstruction(const std::string& hash)
{
  AutoThreadLock lock(&itsThreadLock);
  uint idx = itsImageCounter % 100;
  itsImagesUnderConstruction[idx] = hash;
  itsImageCounter++;
  return idx;
}





/*! \brief GridGui: Clears the slot of an image whose construction has ended. */

void Plugin::endImageConstruction(uint idx)
{
  AutoThreadLock lock(&itsThreadLock);
  itsImagesUnderConstruction[idx] = "";
}





/*! \brief GridGui: Adds a generated image to the image cache. */

void Plugin::addCachedImage(const std::string& hash,const std::string& fname)
{
  AutoThreadLock lock(&itsThreadLock);
  if (itsImages.find(hash) == itsImages.end())
    itsImages.insert(std::pair<std::string,std::string>(hash,fname));
}





/*! \brief GridGui: Loads the image with the given hash into the response, waiting at most
    30 seconds while another thread is generating it. Returns false if the image must be
    generated by the caller. */

bool Plugin::waitForCachedImage(const std::string& hash,Spine::HTTP::Response &theResponse)
{
  time_t endTime = time(nullptr) + 30;

  while (time(nullptr) < endTime)
  {
    // ### Use the requested image if it has been generated earlier.

    if (loadCachedImage(hash,theResponse))
      return true;

    // ### Another thread may be generating the requested image. Let's wait until it is ready
    // ### and can be found from the itsImages list.

    if (!isImageUnderConstruction(hash))
      return false;

    time_usleep(0,10000);
  }
  return false;
}





/*! \brief GridGui: The file name and the modification time of a color map, which identify
    its version in the image hashes. */

std::string Plugin::colorMapVersion(const std::string& colorMap)
{
  if (colorMap.empty() ||  strcasecmp(colorMap.c_str(),"None") == 0)
    return ":";

  T::ColorMapFile *colorMapFile = getColorMapFile(colorMap);
  if (colorMapFile == nullptr)
    return ":";

  return colorMapFile->getFilename() + ":" + std::to_string(C_UINT(colorMapFile->getLastModificationTime()));
}





/*! \brief GridGui: Page image. */

int Plugin::page_image(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string producerIdStr = session.getAttribute(ATTR_PRODUCER_ID);
    std::string generationIdStr = session.getAttribute(ATTR_GENERATION_ID);
    std::string parameterIdStr = session.getAttribute(ATTR_PARAMETER_ID);
    std::string levelIdStr = session.getAttribute(ATTR_LEVEL_ID);
    std::string levelStr = session.getAttribute(ATTR_LEVEL);
    std::string geometryIdStr = session.getAttribute(ATTR_GEOMETRY_ID);
    std::string producerNameStr = session.getAttribute(ATTR_PRODUCER_NAME);
    std::string forecastTypeStr = session.getAttribute(ATTR_FORECAST_TYPE);
    std::string forecastNumberStr = session.getAttribute(ATTR_FORECAST_NUMBER);
    std::string presentation = session.getAttribute(ATTR_PRESENTATION);
    std::string projectionIdStr = session.getAttribute(ATTR_PROJECTION_ID);
    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);
    std::string timeStr = session.getAttribute(ATTR_TIME);
    std::string hueStr = session.getAttribute(ATTR_HUE);
    std::string saturationStr = session.getAttribute(ATTR_SATURATION);
    std::string blurStr = session.getAttribute(ATTR_BLUR);
    std::string coordinateLinesStr = session.getAttribute(ATTR_COORDINATE_LINES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string opacityStr = session.getAttribute(ATTR_OPACITY);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string streamColorStr = session.getAttribute(ATTR_STREAM_COLOR);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);
    std::string landShadingLightStr = session.getAttribute(ATTR_LAND_SHADING_LIGHT);
    std::string landShadingShadowStr = session.getAttribute(ATTR_LAND_SHADING_SHADOW);
    std::string landShadingPositionStr = session.getAttribute(ATTR_LAND_SHADING_POS);
    std::string landColorPosStr = session.getAttribute(ATTR_LAND_COLOR_POS);
    std::string seaShadingLightStr = session.getAttribute(ATTR_SEA_SHADING_LIGHT);
    std::string seaShadingShadowStr = session.getAttribute(ATTR_SEA_SHADING_SHADOW);
    std::string seaShadingPositionStr = session.getAttribute(ATTR_SEA_SHADING_POS);
    std::string seaColorPosStr = session.getAttribute(ATTR_SEA_COLOR_POS);

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    const std::string colorMapVersionStr = colorMapVersion(colorMap);

    std::string hash = "Image:" + fileIdStr + ":" + messageIndexStr + ":" + hueStr + ":" + saturationStr + ":" +
      blurStr + ":" + coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + colorMapVersionStr + ":" + missingStr + ":" +
      landShadingLightStr + ":" + landShadingShadowStr  + ":" + landShadingPositionStr  + ":" + landColorPosStr + ":" +
      seaShadingLightStr + ":" + seaShadingShadowStr  + ":" + seaShadingPositionStr  + ":" + seaColorPosStr + ":" + opacityStr + ":" + streamColorStr;

    // A difference image of the new user interface: the second field minus this one
    std::string fileId2Str = session.getAttribute(ATTR_FILE_ID2);
    std::string messageIndex2Str = session.getAttribute(ATTR_MESSAGE_INDEX2);
    std::string differenceRangeStr = session.getAttribute(ATTR_DIFFERENCE_RANGE);
    if (!fileId2Str.empty())
      hash += ":Difference:" + fileId2Str + ":" + messageIndex2Str + ":" + differenceRangeStr;

    const std::size_t seed = Fmi::hash(hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    if (auto status = conditionalResponseStatus(theRequest, seedStr))
      return *status;

    if (waitForCachedImage(hash,theResponse))
      return HTTP::Status::ok;

    // ### It seems that we should generated the requested image by ourselves.

    uint idx = startImageConstruction(hash);

    try
    {
      std::string fname = itsImageCache_dir + "/grid-gui-image_" + std::to_string(getTime()) + ".png";

      ImagePaintParameters params;

      params.imageFile = fname;
      params.fileId = toUInt64(fileIdStr);
      params.messageIndex = toUInt32(messageIndexStr);
      params.geometryId = toInt32(geometryIdStr);
      params.projectionId = toUInt32(projectionIdStr);
      params.zeroIsMissing = false;
      params.paint_alpha = toUInt32(opacityStr);
      params.paint_colorMapName = colorMap;
      params.paint_hue = toUInt8(hueStr);
      params.paint_saturation = toUInt8(saturationStr);
      params.paint_blur = toUInt8(blurStr);
      params.stream_step = 0;
      params.stream_minLength = 0;
      params.stream_maxLength = 0;
      params.stream_color = getColorValue(streamColorStr);
      params.stream_animation = false;
      params.landBorder_color = getColorValue(landBorderStr);
      params.landColor = getColorValue(landMaskStr);
      params.landColor_position = toInt32(landColorPosStr);
      params.landShading_light = toInt32(landShadingLightStr);
      params.landShading_shadow = toInt32(landShadingShadowStr);
      params.landShading_position = toInt32(landShadingPositionStr);
      params.seaColor = getColorValue(seaMaskStr);
      params.seaColor_position = toInt32(seaColorPosStr);
      params.seaShading_light = toInt32(seaShadingLightStr);
      params.seaShading_shadow = toInt32(seaShadingShadowStr);
      params.seaShading_position = toInt32(seaShadingPositionStr);
      params.coordinateLine_color = getColorValue(coordinateLinesStr);
      if (!fileId2Str.empty())
      {
        params.fileId2 = toUInt64(fileId2Str);
        params.messageIndex2 = toUInt32(messageIndex2Str);
        if (!differenceRangeStr.empty())
          params.differenceRange = toDouble(differenceRangeStr);
      }

      saveImage(params);

      if (loadImage(fname.c_str(),theResponse))
        addCachedImage(hash,fname);
      endImageConstruction(idx);
    }
    catch (...)
    {
      endImageConstruction(idx);
      Fmi::Exception exception(BCP, "Operation failed!", nullptr);
      throw exception;
    }

    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Page streams. */

int Plugin::page_streams(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  return page_streamsImpl(theRequest, theResponse, session, /*animation=*/false);
}





/*! \brief GridGui: Page streams animation. */

int Plugin::page_streamsAnimation(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  return page_streamsImpl(theRequest, theResponse, session, /*animation=*/true);
}




/*! \brief GridGui: Shared rendering helper for the streams and streams-animation pages. */

int Plugin::page_streamsImpl(const HTTP::Request &theRequest,
                             HTTP::Response &theResponse,
                             Session& session,
                             bool animation)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string producerIdStr = session.getAttribute(ATTR_PRODUCER_ID);
    std::string generationIdStr = session.getAttribute(ATTR_GENERATION_ID);
    std::string parameterIdStr = session.getAttribute(ATTR_PARAMETER_ID);
    std::string levelIdStr = session.getAttribute(ATTR_LEVEL_ID);
    std::string levelStr = session.getAttribute(ATTR_LEVEL);
    std::string geometryIdStr = session.getAttribute(ATTR_GEOMETRY_ID);
    std::string producerNameStr = session.getAttribute(ATTR_PRODUCER_NAME);
    std::string forecastTypeStr = session.getAttribute(ATTR_FORECAST_TYPE);
    std::string forecastNumberStr = session.getAttribute(ATTR_FORECAST_NUMBER);
    std::string presentation = session.getAttribute(ATTR_PRESENTATION);
    std::string projectionIdStr = session.getAttribute(ATTR_PROJECTION_ID);
    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);
    std::string timeStr = session.getAttribute(ATTR_TIME);
    std::string hueStr = session.getAttribute(ATTR_HUE);
    std::string saturationStr = session.getAttribute(ATTR_SATURATION);
    std::string blurStr = session.getAttribute(ATTR_BLUR);
    std::string coordinateLinesStr = session.getAttribute(ATTR_COORDINATE_LINES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string opacityStr = session.getAttribute(ATTR_OPACITY);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string streamColorStr = session.getAttribute(ATTR_STREAM_COLOR);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);
    std::string landShadingLightStr = session.getAttribute(ATTR_LAND_SHADING_LIGHT);
    std::string landShadingShadowStr = session.getAttribute(ATTR_LAND_SHADING_SHADOW);
    std::string landShadingPositionStr = session.getAttribute(ATTR_LAND_SHADING_POS);
    std::string landColorPosStr = session.getAttribute(ATTR_LAND_COLOR_POS);
    std::string seaShadingLightStr = session.getAttribute(ATTR_SEA_SHADING_LIGHT);
    std::string seaShadingShadowStr = session.getAttribute(ATTR_SEA_SHADING_SHADOW);
    std::string seaShadingPositionStr = session.getAttribute(ATTR_SEA_SHADING_POS);
    std::string seaColorPosStr = session.getAttribute(ATTR_SEA_COLOR_POS);

    const std::string colorMapVersionStr = colorMapVersion(colorMap);

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    const char *hashPrefix = animation ? "StreamsAnimation:" : "Streams:";
    std::string hash = std::string(hashPrefix) + fileIdStr + ":" + messageIndexStr + ":" + hueStr + ":" + saturationStr + ":" +
      blurStr + ":" + coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + colorMapVersionStr + ":" + missingStr + ":" +
      minLengthStr + ":" + maxLengthStr + ":" + stepStr + ":" + streamColorStr + ":" +
      landShadingLightStr + ":" + landShadingShadowStr  + ":" + landShadingPositionStr  + ":" + landColorPosStr + ":" +
      seaShadingLightStr + ":" + seaShadingShadowStr  + ":" + seaShadingPositionStr  + ":" + seaColorPosStr + ":" + opacityStr;

    const std::size_t seed = Fmi::hash(hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    if (auto status = conditionalResponseStatus(theRequest, seedStr))
      return *status;

    if (waitForCachedImage(hash,theResponse))
      return HTTP::Status::ok;

    uint idx = startImageConstruction(hash);

    try
    {
      const char *fileExt = animation ? ".webp" : ".png";
      std::string fname = itsImageCache_dir + "/grid-gui-image_" + std::to_string(getTime()) + fileExt;

      ImagePaintParameters params;

      params.imageFile = fname;
      params.fileId = toUInt64(fileIdStr);
      params.messageIndex = toUInt32(messageIndexStr);
      params.geometryId = toInt32(geometryIdStr);
      params.projectionId = toUInt32(projectionIdStr);
      params.zeroIsMissing = false;
      params.paint_alpha = 255;
      params.paint_colorMapName = colorMap;
      params.paint_hue = toUInt8(hueStr);
      params.paint_saturation = toUInt8(saturationStr);
      params.paint_blur = toUInt8(blurStr);
      params.stream_step = toUInt32(stepStr);
      params.stream_minLength = toUInt32(minLengthStr);
      params.stream_maxLength = toUInt32(maxLengthStr);
      params.stream_color = getColorValue(streamColorStr);
      params.stream_animation = animation;
      params.landBorder_color = getColorValue(landBorderStr);
      params.landColor = getColorValue(landMaskStr);
      params.landColor_position = toInt32(landColorPosStr);
      params.landShading_light = toInt32(landShadingLightStr);
      params.landShading_shadow = toInt32(landShadingShadowStr);
      params.landShading_position = toInt32(landShadingPositionStr);
      params.seaColor = getColorValue(seaMaskStr);
      params.seaColor_position = toInt32(seaColorPosStr);
      params.seaShading_light = toInt32(seaShadingLightStr);
      params.seaShading_shadow = toInt32(seaShadingShadowStr);
      params.seaShading_position = toInt32(seaShadingPositionStr);
      params.coordinateLine_color = getColorValue(coordinateLinesStr);

      saveImage(params);

      if (loadImage(fname.c_str(),theResponse))
        addCachedImage(hash,fname);

      endImageConstruction(idx);
    }
    catch (...)
    {
      endImageConstruction(idx);
      Fmi::Exception exception(BCP, "Operation failed!", nullptr);
      throw exception;
    }

    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





namespace
{
/*! \brief The part of the world "lon0,lat0,lon1,lat1" of a map, and its size in pixels: at
    most 1200 pixels on the longer side. None for the whole world, whose size is not changed. */

std::optional<MapArea> parseMapArea(const std::string& areaStr,uint& columns,uint& rows)
{
  if (areaStr.empty())
    return {};

  std::vector<double> v;
  splitString(areaStr,',',v);
  if (v.size() != 4 || !(v[0] >= -180 && v[0] < v[2] && v[2] <= 180 && v[1] >= -90 && v[1] < v[3] && v[3] <= 90))
    throw Fmi::Exception(BCP,"The map area must be lon0,lat0,lon1,lat1 within -180..180 and -90..90").addParameter("Area",areaStr);

  const double aspect = (v[2] - v[0]) / (v[3] - v[1]);
  columns = (aspect >= 1 ? 1200 : std::max(1U, C_UINT(std::lround(1200 * aspect))));
  rows = (aspect >= 1 ? std::max(1U, C_UINT(std::lround(1200 / aspect))) : 1200);
  return MapArea{v[0], v[3], (v[2] - v[0]) / columns, (v[3] - v[1]) / rows};
}
}  // namespace





/*! \brief GridGui: Page map. */

int Plugin::page_map(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string producerIdStr = session.getAttribute(ATTR_PRODUCER_ID);
    std::string generationIdStr = session.getAttribute(ATTR_GENERATION_ID);
    std::string parameterIdStr = session.getAttribute(ATTR_PARAMETER_ID);
    std::string levelIdStr = session.getAttribute(ATTR_LEVEL_ID);
    std::string levelStr = session.getAttribute(ATTR_LEVEL);
    std::string geometryIdStr = session.getAttribute(ATTR_GEOMETRY_ID);
    std::string producerNameStr = session.getAttribute(ATTR_PRODUCER_NAME);
    std::string forecastTypeStr = session.getAttribute(ATTR_FORECAST_TYPE);
    std::string forecastNumberStr = session.getAttribute(ATTR_FORECAST_NUMBER);
    std::string presentation = session.getAttribute(ATTR_PRESENTATION);
    std::string projectionIdStr = session.getAttribute(ATTR_PROJECTION_ID);
    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);
    std::string timeStr = session.getAttribute(ATTR_TIME);
    std::string hueStr = session.getAttribute(ATTR_HUE);
    std::string saturationStr = session.getAttribute(ATTR_SATURATION);
    std::string blurStr = session.getAttribute(ATTR_BLUR);
    std::string coordinateLinesStr = session.getAttribute(ATTR_COORDINATE_LINES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string streamColorStr = session.getAttribute(ATTR_STREAM_COLOR);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);

    const std::string colorMapVersionStr = colorMapVersion(colorMap);

    std::string hash = "Map:" + fileIdStr + ":" + messageIndexStr + ":" + hueStr + ":" + saturationStr + ":" +
      blurStr + ":" + coordinateLinesStr + ":" + landBorderStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + colorMapVersionStr + ":" + missingStr;

    // A part of the world (the new user interface): "lon0,lat0,lon1,lat1"
    uint columns = 1800;
    uint rows = 900;
    const std::string areaStr = session.getAttribute(ATTR_MAP_AREA);
    const auto area = parseMapArea(areaStr,columns,rows);
    if (area)
      hash += ":Area:" + areaStr;

    const std::size_t seed = Fmi::hash(hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    if (auto status = conditionalResponseStatus(theRequest, seedStr))
      return *status;

    if (waitForCachedImage(hash,theResponse))
      return HTTP::Status::ok;

    uint idx = startImageConstruction(hash);

    try
    {
      const double lon0 = (area ? area->lon0 : -180);
      const double lat1 = (area ? area->lat1 : 90);
      uint coordinateLines = getColorValue(coordinateLinesStr);

      T::ParamValue_vec values;
      double_vec modificationParameters;
      int result = dataServer->getGridValueVectorByRectangle(0,toUInt64(fileIdStr),toUInt32(messageIndexStr),T::CoordinateTypeValue::LATLON_COORDINATES,columns,rows,lon0,lat1,(area ? area->dlon : 360/C_DOUBLE(columns)),(area ? -area->dlat : -180/C_DOUBLE(rows)),T::AreaInterpolationMethod::Nearest,0,modificationParameters,values);
      if (result != 0)
      {
        std::ostringstream ostr;
        ostr << "<HTML><BODY>\n";
        ostr << "DataServer request 'getGridValuesByArea()' failed : " << result << "\n";
        ostr << "</BODY></HTML>\n";
        theResponse.setContent(std::string(ostr.str()));
        theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
        return HTTP::Status::ok;
      }

      uint landBorder = getColorValue(landBorderStr);

      std::string fname = "/" + itsImageCache_dir + "/grid-gui-image_" + std::to_string(getTime()) + ".png";
      saveMap(fname.c_str(),columns,rows,values,toUInt8(hueStr),toUInt8(saturationStr),toUInt8(blurStr),coordinateLines,landBorder,landMaskStr,seaMaskStr,colorMap,missingStr,area ? &*area : nullptr);

      if (loadImage(fname.c_str(),theResponse))
        addCachedImage(hash,fname);

      endImageConstruction(idx);
    }
    catch (...)
    {
      endImageConstruction(idx);
      Fmi::Exception exception(BCP, "Operation failed!", nullptr);
      throw exception;
    }

    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Get level ids. */

void Plugin::getLevelIds(const T::ContentInfoList& contentInfoList,std::set<int>& levelIds)
{
  FUNCTION_TRACE
  try
  {
    uint len = contentInfoList.getLength();

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if (g->mFmiParameterLevelId >= 0)
      {
        if (levelIds.find(g->mFmiParameterLevelId) == levelIds.end())
        {
          levelIds.insert(g->mFmiParameterLevelId);
        }
      }
      /*
      else
      if (g->mGrib1ParameterLevelId > 0)
      {
        if (levelIds.find(g->mGrib1ParameterLevelId+1000) == levelIds.end())
        {
          levelIds.insert(g->mGrib1ParameterLevelId+1000);
        }
      }
      else
      if (g->mGrib2ParameterLevelId > 0)
      {
        if (levelIds.find(g->mGrib2ParameterLevelId+2000) == levelIds.end())
        {
          levelIds.insert(g->mGrib2ParameterLevelId+2000);
        }
      }
      */
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Get levels. */

void Plugin::getLevels(const T::ContentInfoList& contentInfoList,int levelId,std::set<int>& levels)
{
  FUNCTION_TRACE
  try
  {
    uint len = contentInfoList.getLength();
    int id = levelId % 1000;

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if ((levelId >= 0  &&  levelId < 1000  &&  id == g->mFmiParameterLevelId)/* ||
          (levelId >= 1000  &&  levelId < 2000  &&  id == g->mGrib1ParameterLevelId) ||
          (levelId >= 2000  &&  id == g->mGrib2ParameterLevelId)*/)
      {
        if (levels.find(g->mParameterLevel) == levels.end())
        {
          levels.insert(g->mParameterLevel);
        }
      }
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Get forecast types. */

void Plugin::getForecastTypes(const T::ContentInfoList& contentInfoList,int levelId,int level,std::set<int>& forecastTypes)
{
  FUNCTION_TRACE
  try
  {
    uint len = contentInfoList.getLength();
    int id = levelId % 1000;

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if ((levelId >= 0  &&  levelId < 1000  &&  id == g->mFmiParameterLevelId)/* ||
          (levelId >= 1000  &&  levelId < 2000  &&  id == g->mGrib1ParameterLevelId) ||
          (levelId >= 2000  &&  id == g->mGrib2ParameterLevelId)*/)
      {
        if (level == g->mParameterLevel)
        {
          if (forecastTypes.find(g->mForecastType) == forecastTypes.end())
          {
            forecastTypes.insert(g->mForecastType);
          }
        }
      }
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Get forecast numbers. */

void Plugin::getForecastNumbers(const T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,std::set<int>& forecastNumbers)
{
  FUNCTION_TRACE
  try
  {
    uint len = contentInfoList.getLength();
    int id = levelId % 1000;

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if ((levelId >= 0  &&  levelId < 1000  &&  id == g->mFmiParameterLevelId)/* ||
          (levelId >= 1000  &&  levelId < 2000  &&  id == g->mGrib1ParameterLevelId) ||
          (levelId >= 2000  &&  id == g->mGrib2ParameterLevelId)*/)
      {
        if (level == g->mParameterLevel)
        {
          if (forecastType == g->mForecastType)
          {
            if (forecastNumbers.find(g->mForecastNumber) == forecastNumbers.end())
            {
              forecastNumbers.insert(g->mForecastNumber);
            }
          }
        }
      }
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Get geometries. */

void Plugin::getGeometries(const T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,int forecastNumber,std::set<int>& geometries)
{
  FUNCTION_TRACE
  try
  {
    uint len = contentInfoList.getLength();
    int id = levelId % 1000;

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if ((levelId >= 0  &&  levelId < 1000  &&  id == g->mFmiParameterLevelId) /*||
          (levelId >= 1000  &&  levelId < 2000  &&  id == g->mGrib1ParameterLevelId) ||
          (levelId >= 2000  &&  id == g->mGrib2ParameterLevelId)*/)
      {
        if (level == g->mParameterLevel)
        {
          if (forecastType == g->mForecastType)
          {
            if (forecastNumber == g->mForecastNumber)
            {
              if (geometries.find(g->mGeometryId) == geometries.end())
              {
                geometries.insert(g->mGeometryId);
              }
            }
          }
        }
      }
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Get fmi key. */

std::string Plugin::getFmiKey(const std::string& producerName,const T::ContentInfo& contentInfo)
{
  FUNCTION_TRACE
  try
  {
    char buf[200];
    char *p = buf;

    if (strlen(contentInfo.getFmiParameterName()) > 0)
      p += sprintf(p,"%s",contentInfo.getFmiParameterName());
    else if (contentInfo.mFmiParameterId > 0)
      p += sprintf(p,"FMI-%u",contentInfo.mFmiParameterId);

    p += sprintf(p,":%s",producerName.c_str());

    if (contentInfo.mGeometryId > 0)
      p += sprintf(p,":%d",contentInfo.mGeometryId);
    else
      p += sprintf(p,":");

    if (contentInfo.mFmiParameterLevelId > 0)
      p += sprintf(p,":%d",contentInfo.mFmiParameterLevelId);
    else
      p += sprintf(p,":");

    p += sprintf(p,":%d",contentInfo.mParameterLevel);

    if (contentInfo.mForecastType >= 0)
    {
      if (contentInfo.mForecastNumber >= 0)
        sprintf(p,":%d:%d",contentInfo.mForecastType,contentInfo.mForecastNumber);
      else
        sprintf(p,":%d",contentInfo.mForecastType);
    }

    return std::string(buf);
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Get generations. */

void Plugin::getGenerations(T::GenerationInfoList& generationInfoList,std::set<std::string>& generations)
{
  FUNCTION_TRACE
  try
  {
    uint len = generationInfoList.getLength();
    for (uint t=0; t<len; t++)
    {
      T::GenerationInfo *g = generationInfoList.getGenerationInfoByIndex(t);
      if (generations.find(g->mName) == generations.end())
      {
        generations.insert(g->mName);
      }
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





/*! \brief GridGui: Init session. */

void Plugin::initSession(Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();

    session.setAttribute(ATTR_PAGE,"main");
    session.setAttribute(ATTR_PRODUCER_ID,"");
    session.setAttribute(ATTR_PRODUCER_NAME,"");
    session.setAttribute(ATTR_GENERATION_ID,"");
    session.setAttribute(ATTR_PARAMETER_ID,"");
    session.setAttribute(ATTR_LEVEL_ID,"");
    session.setAttribute(ATTR_LEVEL,"");
    session.setAttribute(ATTR_FORECAST_TYPE,"");
    session.setAttribute(ATTR_FORECAST_NUMBER,"");
    session.setAttribute(ATTR_GEOMETRY_ID,"");
    session.setAttribute(ATTR_PRESENTATION,"Image");
    session.setAttribute(ATTR_PROJECTION_ID,"");
    session.setAttribute(ATTR_FILE_ID,"");
    session.setAttribute(ATTR_MESSAGE_INDEX,"0");
    session.setAttribute(ATTR_TIME_GROUP_TYPE,"Month");
    session.setAttribute(ATTR_TIME_GROUP,"");
    session.setAttribute(ATTR_TIME,"");
    session.setAttribute(ATTR_HUE,"128");
    session.setAttribute(ATTR_SATURATION,"60");
    session.setAttribute(ATTR_BLUR,"1");
    session.setAttribute(ATTR_COORDINATE_LINES,"Grey");
    session.setAttribute(ATTR_LAND_BORDER,"DarkSlateGrey");
    session.setAttribute(ATTR_LAND_MASK,"LightGrey");
    session.setAttribute(ATTR_SEA_MASK,"LightCyan");
    session.setAttribute(ATTR_COLOR_MAP,"None");
    session.setAttribute(ATTR_OPACITY,"255");
    session.setAttribute(ATTR_MISSING,"Default");
    session.setAttribute(ATTR_STEP,"14");
    session.setAttribute(ATTR_MIN_LENGTH,"2");
    session.setAttribute(ATTR_MAX_LENGTH,"64");
    session.setAttribute(ATTR_STREAM_COLOR,"DarkSlateGrey");
    session.setAttribute(ATTR_UNIT,"");
    session.setAttribute(ATTR_FMI_KEY,"");
    session.setAttribute(ATTR_X,"");
    session.setAttribute(ATTR_Y,"");
    session.setAttribute(ATTR_PROJECTION_LOCK,"");
    session.setAttribute(ATTR_LAND_SHADING_LIGHT,"128");
    session.setAttribute(ATTR_LAND_SHADING_SHADOW,"384");
    session.setAttribute(ATTR_LAND_SHADING_POS,"2");
    session.setAttribute(ATTR_LAND_COLOR_POS,"1");
    session.setAttribute(ATTR_SEA_SHADING_LIGHT,"128");
    session.setAttribute(ATTR_SEA_SHADING_SHADOW,"384");
    session.setAttribute(ATTR_SEA_SHADING_POS,"2");
    session.setAttribute(ATTR_SEA_COLOR_POS,"1");
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: Emit the JavaScript helper block used by the page_main HTML page. */

void Plugin::page_main_writeJavascript(std::ostringstream& output)
{
  output << "<SCRIPT>\n";

  output << "var backColor;\n";
  output << "var invisible = '#fefefe';\n";
  output << "var buttonColor = '#808080';\n";

  output << "function getPage(obj,frm,url)\n";
  output << "{\n";
  output << "  frm.location.href=url;\n";
  output << "}\n";

  output << "function setImage(img,url)\n";
  output << "{\n";
  output << "  img.src = url;\n";
  output << "}\n";

  output << "function mouseOver(obj)\n";
  output << "{\n";
  output << "  if (obj.bgColor != invisible)\n";
  output << "  {\n";
  output << "    backColor = obj.bgColor;\n";
  output << "    obj.bgColor='#FF8040';\n";
  output << "  }\n";
  output << "}\n";

  output << "function mouseOut(obj)\n";
  output << "{\n";
  output << "  if (obj.bgColor != invisible)\n";
  output << "  {\n";
  output << "    obj.bgColor=backColor;\n";
  output << "  }\n";
  output << "}\n";

  output << "function keyDown(event,obj,img,url)\n";
  output << "{\n";
  output << "  var index = obj.selectedIndex\n";
  output << "  var keyCode = ('which' in event) ? event.which : event.keyCode;\n";
  output << "  if (keyCode == 38  &&  index > 0) index--;\n";
  output << "  if (keyCode == 40) index++;\n";

  output << "  setImage(img,url + obj.options[index].value);\n";
  output << "}\n";

  output << "function setText(id,txt)\n";
  output << "{\n";
  output << "  document.getElementById(id).innerHTML = txt;\n";
  output << "}\n";

  output << "function httpGet(theUrl)\n";
  output << "{\n";
  output << "  var xmlHttp = new XMLHttpRequest();\n";
  output << "  xmlHttp.open(\"GET\", theUrl, false );\n";
  output << "  xmlHttp.send( null );\n";
  output << "  return xmlHttp.responseText;\n";
  output << "}\n";

  output << "function getImageCoords(event,img,fileId,messageIndex,presentation,sessionParam) {\n";
  output << "  var posX = event.offsetX?(event.offsetX):event.pageX-img.offsetLeft;\n";
  output << "  var posY = event.offsetY?(event.offsetY):event.pageY-img.offsetTop;\n";
  output << "  var prosX = posX / img.width;\n";
  output << "  var prosY = posY / img.height;\n";
  output << R"(  var url = "/grid-gui?session=" + sessionParam + ";)" << ATTR_PAGE << "=value;" << ATTR_PRESENTATION << "=\" + presentation + \";" << ATTR_FILE_ID << "=\" + fileId + \";" << ATTR_MESSAGE_INDEX << "=\" + messageIndex + \";" << ATTR_X << "=\" + prosX + \";" << ATTR_Y << "=\" + prosY;\n";
  output << "  var txt = httpGet(url);\n";
  output << "  document.getElementById('gridValue').value = txt;\n";

  output << "}\n";
  output << "</SCRIPT>\n";
}




/*! \brief GridGui: Page main. */

// The state shared by the parts of the main page (Plugin::page_main)

struct PageMainState
{
  std::shared_ptr<ContentServer::ServiceInterface> contentServer{};
  std::string producerIdStr{};
  std::string generationIdStr{};
  std::string parameterIdStr{};
  std::string levelIdStr{};
  std::string levelStr{};
  std::string geometryIdStr{};
  std::string producerNameStr{};
  std::string forecastTypeStr{};
  std::string forecastNumberStr{};
  std::string presentation{};
  std::string projectionIdStr{};
  std::string fileIdStr{};
  std::string messageIndexStr{};
  std::string timeStr{};
  std::string hueStr{};
  std::string saturationStr{};
  std::string blurStr{};
  std::string coordinateLinesStr{};
  std::string landBorderStr{};
  std::string landMaskStr{};
  std::string seaMaskStr{};
  std::string colorMap{};
  std::string opacityStr{};
  std::string missingStr{};
  std::string stepStr{};
  std::string minLengthStr{};
  std::string maxLengthStr{};
  std::string streamColorStr{};
  std::string unitStr{};
  std::string fmiKeyStr{};
  std::string timeGroupTypeStr{};
  std::string timeGroupStr{};
  std::string projectionLockStr{};
  std::string landShadingLightStr{};
  std::string landShadingShadowStr{};
  std::string landShadingPositionStr{};
  std::string landColorPosStr{};
  std::string seaShadingLightStr{};
  std::string seaShadingShadowStr{};
  std::string seaShadingPositionStr{};
  std::string seaColorPosStr{};
  time_t requiredAccessTime{};
  uint landshade_light{};
  uint landshade_shadow{};
  uint landshade_pos{};
  uint landcol_pos{};
  uint seashade_light{};
  uint seashade_shadow{};
  uint seashade_pos{};
  uint seacol_pos{};
  uint opacity{};
  std::ostringstream output{};
  std::ostringstream ostr1{};
  std::ostringstream ostr2{};
  std::ostringstream ostr3{};
  std::ostringstream ostr4{};
  T::ProducerInfoList producerInfoList{};
  uint len{};
  T::ProducerId producerId{};
  T::GenerationInfoList generationInfoList{};
  T::GenerationInfoList generationInfoList2{};
  T::GenerationId generationId{};
  bool generationNotReady{};
  std::set<std::string> generations{};
  std::string originTime{};
  std::string paramDescription{};
  std::set<std::string> paramKeyList{};
  T::ContentInfoList contentInfoList{};
  int levelId{};
  std::set<int> levelIds{};
  T::ParamLevel level{};
  std::set<int> levels{};
  short forecastType{};
  std::set<int> forecastTypes{};
  short forecastNumber{};
  std::set<int> forecastNumbers{};
  T::GeometryId geometryId{};
  std::set<int> geometries{};
  std::string pTime{};
  std::set<std::string> timeGroupStrList{};
  uint timeGroupType{};
  bool useTimeGroup{};
  uint a{};
  std::string prevTime{};
  uint tCount{};
  T::ContentInfo *prevCont = nullptr;
  T::ContentInfo *currentCont = nullptr;
  T::ContentInfo *nextCont = nullptr;
  T::ContentInfoList contentInfoListByLevels{};
  uint lCount{};
  std::string aggregation{};
  std::string processing{};
};



namespace
{
const char *layerPosition[] = {"None","Bottom","Top",nullptr};
const char *timeGroupTypes[] = {"All","Day","Month","Year",nullptr};
int timeGroupLen[] = {15,8,6,4};
const char *modes[] = {"Image","Map","Streams","StreamsAnimation","Info","Table(sample)","Coordinates(sample)","Message",nullptr};
// The aggregation description of the content, or an empty string

std::string aggregationText(const T::ContentInfo& cont)
{
  std::string aggregation;
  if (cont.mAggregationId > 0)
  {
    Identification::AggregationDef aggregationDef;
    if (Identification::gridDef.getFmiAggregationDef(cont.mAggregationId,aggregationDef))
    {
      aggregation = " / Aggregation: " + aggregationDef.mDescription;
      if (cont.mAggregationPeriod)
      {
        if ((cont.mAggregationPeriod % 60) == 0)
          aggregation = aggregation + " (" + std::to_string(cont.mAggregationPeriod/60) + " hours)";
        else
          aggregation = aggregation + " (" + std::to_string(cont.mAggregationPeriod) + " minutes)";
      }
    }
  }
  return aggregation;
}



// The processing type description of the content, or an empty string

std::string processingText(const T::ContentInfo& cont)
{
  std::string processing;
  if (cont.mProcessingTypeId > 0)
  {
    Identification::ProcessingTypeDef processingTypeDef;
    if (Identification::gridDef.getFmiProcessingTypeDef(cont.mProcessingTypeId,processingTypeDef))
    {
      processing = " / Processing: " + processingTypeDef.mDescription;
      if (cont.mProcessingTypeValue1 != ParamValueMissing)
      {
        processing = processing  + " (" + std::to_string(cont.mProcessingTypeValue1);
        if (cont.mProcessingTypeValue2 != ParamValueMissing)
          processing = processing + ", " + std::to_string(cont.mProcessingTypeValue2) + ")";
        processing = processing + ")";
      }
    }
  }
  return processing;
}

// Options first, first+step, ... below end for a numeric selection

void writeNumberOptions(std::ostringstream& out,Session& session,const char *attr,uint selected,uint first,uint end,uint step)
{
  for (uint a=first; a<end; a = a + step)
  {
    if (a == selected)
    {
      out << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
      session.setAttribute(attr,a);
    }
    else
    {
      out << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
    }
  }
}

// Is the content of the selected geometry, forecast type and number, later
// than the previous time and in the selected time group?

bool isSelectableTime(const PageMainState& ps,T::ContentInfo& g)
{
  if (g.mGeometryId != ps.geometryId)
    return false;
  std::string ft = g.getForecastTime();
  if (!(ps.prevTime < ft  &&  (!ps.useTimeGroup  || (ps.useTimeGroup  &&  ft.substr(0,timeGroupLen[ps.timeGroupType]) == ps.timeGroupStr))))
    return false;
  return ps.forecastType == g.mForecastType  &&  ps.forecastNumber == g.mForecastNumber;
}

}  // namespace



/*! \brief GridGui: Read the selection attributes of the session, reset the ones depending on a changed selection and start the page. */

void Plugin::page_main_selectionAttributes(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    //session.print(std::cout,0,0);

    ps.contentServer = itsGridEngine->getContentServer_sptr();

    ps.producerIdStr = session.getAttribute(ATTR_PRODUCER_ID);
    ps.generationIdStr = session.getAttribute(ATTR_GENERATION_ID);
    ps.parameterIdStr = session.getAttribute(ATTR_PARAMETER_ID);
    ps.levelIdStr = session.getAttribute(ATTR_LEVEL_ID);
    ps.levelStr = session.getAttribute(ATTR_LEVEL);
    ps.geometryIdStr = session.getAttribute(ATTR_GEOMETRY_ID);
    ps.producerNameStr = session.getAttribute(ATTR_PRODUCER_NAME);
    ps.forecastTypeStr = session.getAttribute(ATTR_FORECAST_TYPE);
    ps.forecastNumberStr = session.getAttribute(ATTR_FORECAST_NUMBER);
    ps.presentation = session.getAttribute(ATTR_PRESENTATION);
    ps.projectionIdStr = session.getAttribute(ATTR_PROJECTION_ID);
    ps.fileIdStr = session.getAttribute(ATTR_FILE_ID);
    ps.messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);
    ps.timeStr = session.getAttribute(ATTR_TIME);
    ps.hueStr = session.getAttribute(ATTR_HUE);
    ps.saturationStr = session.getAttribute(ATTR_SATURATION);
    ps.blurStr = session.getAttribute(ATTR_BLUR);
    ps.coordinateLinesStr = session.getAttribute(ATTR_COORDINATE_LINES);
    ps.landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    ps.landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    ps.seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    ps.colorMap = session.getAttribute(ATTR_COLOR_MAP);
    ps.opacityStr = session.getAttribute(ATTR_OPACITY);
    ps.missingStr = session.getAttribute(ATTR_MISSING);
    ps.stepStr = session.getAttribute(ATTR_STEP);
    ps.minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    ps.maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    ps.streamColorStr = session.getAttribute(ATTR_STREAM_COLOR);
    ps.unitStr = session.getAttribute(ATTR_UNIT);
    ps.fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);
    ps.timeGroupTypeStr = session.getAttribute(ATTR_TIME_GROUP_TYPE);
    ps.timeGroupStr = session.getAttribute(ATTR_TIME_GROUP);
    ps.projectionLockStr = session.getAttribute(ATTR_PROJECTION_LOCK);
    ps.landShadingLightStr = session.getAttribute(ATTR_LAND_SHADING_LIGHT);
    ps.landShadingShadowStr = session.getAttribute(ATTR_LAND_SHADING_SHADOW);
    ps.landShadingPositionStr = session.getAttribute(ATTR_LAND_SHADING_POS);
    ps.landColorPosStr = session.getAttribute(ATTR_LAND_COLOR_POS);
    ps.seaShadingLightStr = session.getAttribute(ATTR_SEA_SHADING_LIGHT);
    ps.seaShadingShadowStr = session.getAttribute(ATTR_SEA_SHADING_SHADOW);
    ps.seaShadingPositionStr = session.getAttribute(ATTR_SEA_SHADING_POS);
    ps.seaColorPosStr = session.getAttribute(ATTR_SEA_COLOR_POS);

    if (session.findAttribute("#",ATTR_PRODUCER_ID))
    {
      ps.generationIdStr = "";
      session.setAttribute(ATTR_GENERATION_ID,"");
    }

    if (ps.generationIdStr.empty() || session.findAttribute("#",ATTR_GENERATION_ID))
    {
      ps.parameterIdStr = "";
      session.setAttribute(ATTR_PARAMETER_ID,"");
    }

    if (ps.parameterIdStr.empty() || session.findAttribute("#",ATTR_PARAMETER_ID))
    {
      ps.levelIdStr = "";
      session.setAttribute(ATTR_LEVEL_ID,"");
    }

    if (ps.levelIdStr.empty() || session.findAttribute("#",ATTR_LEVEL_ID))
    {
      ps.levelStr = "";
      session.setAttribute(ATTR_LEVEL,"");
    }

    if (ps.levelStr.empty() || session.findAttribute("#",ATTR_LEVEL))
    {
      ps.forecastTypeStr = "";
      session.setAttribute(ATTR_FORECAST_TYPE,"");
    }

    if (ps.forecastTypeStr.empty() || session.findAttribute("#",ATTR_FORECAST_TYPE))
    {
      ps.forecastNumberStr = "";
      session.setAttribute(ATTR_FORECAST_NUMBER,"");
    }

    if (ps.forecastNumberStr.empty() || session.findAttribute("#",ATTR_FORECAST_NUMBER))
    {
      ps.geometryIdStr = "";
      session.setAttribute(ATTR_GEOMETRY_ID,"");
    }

    if (ps.geometryIdStr.empty() || session.findAttribute("#",ATTR_GEOMETRY_ID))
    {
      ps.timeGroupStr = "";
      session.setAttribute(ATTR_TIME_GROUP,"");
      ps.timeStr = "";
      session.setAttribute(ATTR_TIME,"");
      if (ps.projectionLockStr.empty())
      {
        ps.projectionIdStr = "";
        session.setAttribute(ATTR_PROJECTION_ID,"");
      }
      ps.fileIdStr = "";
      session.setAttribute(ATTR_FILE_ID,"");
      ps.messageIndexStr = "0";
      session.setAttribute(ATTR_MESSAGE_INDEX,"0");
    }

    if (ps.timeGroupTypeStr.empty() || session.findAttribute("#",ATTR_TIME_GROUP_TYPE))
    {
      ps.timeGroupStr = "";
      session.setAttribute(ATTR_TIME_GROUP,"");
      ps.timeStr = "";
      session.setAttribute(ATTR_TIME,"");
    }

    ps.requiredAccessTime = time(nullptr) + 120;

    if (getFileModificationTime(itsColorFile.c_str()) != itsColors_lastModified)
    {
      loadColorFile();
    }

    loadProducerFile();

    ps.landshade_light = toUInt32(ps.landShadingLightStr);
    ps.landshade_shadow = toUInt32(ps.landShadingShadowStr);
    ps.landshade_pos = toUInt32(ps.landShadingPositionStr);
    ps.landcol_pos = toUInt32(ps.landColorPosStr);

    ps.seashade_light = toUInt32(ps.seaShadingLightStr);
    ps.seashade_shadow = toUInt32(ps.seaShadingShadowStr);
    ps.seashade_pos = toUInt32(ps.seaShadingPositionStr);
    ps.seacol_pos = toUInt32(ps.seaColorPosStr);
    ps.opacity = toUInt32(ps.opacityStr);



    ps.output << "<HTML>\n";
    ps.output << "<BODY>\n";

    page_main_writeJavascript(ps.output);


    ps.ostr1 << "<TABLE width=\"100%\" height=\"100%\">\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The producer selection. */

void Plugin::page_main_producers(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Producers:

    ps.contentServer->getProducerInfoList(0,ps.producerInfoList);
    ps.len = ps.producerInfoList.getLength();
    ps.producerInfoList.sortByName();
    ps.producerId = toUInt32(ps.producerIdStr);

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Producer:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";

    if (ps.len > 0)
    {
      ps.ostr1 << R"(<SELECT style="width:280px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PRODUCER_ID << "=' + this.options[this.selectedIndex].value)\">\n";
      for (uint t=0; t<ps.len; t++)
      {
        T::ProducerInfo *p = ps.producerInfoList.getProducerInfoByIndex(t);

        if (ps.producerId == 0)
        {
          ps.producerId = p->mProducerId;
          ps.producerIdStr = std::to_string(ps.producerId);
          //printf("EMPTY => PRODUCER %s\n",producerIdStr.c_str());
        }

        if (ps.producerId == p->mProducerId)
        {
          ps.producerNameStr = p->mName;
          session.setAttribute(ATTR_PRODUCER_ID,ps.producerId);
          session.setAttribute(ATTR_PRODUCER_NAME,ps.producerNameStr);
          ps.ostr1 << "<OPTION selected value=\"" <<  p->mProducerId << "\">" <<  p->mName << "</OPTION>\n";
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  p->mProducerId << "\">" <<  p->mName << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";
    }
    ps.ostr1 << "</TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The generation selection. */

/*! \brief GridGui: The option of one generation in the generation selection. */

void Plugin::page_main_generationOption(PageMainState& ps,Session& session,const std::string& name)
{
  FUNCTION_TRACE
  try
  {
    T::GenerationInfo *g = ps.generationInfoList.getGenerationInfoByName(name);
    if (g != nullptr && (g->mDeletionTime == 0 || g->mDeletionTime > ps.requiredAccessTime))
    {
      std::string status;
      if (g->mStatus != 1)
        status = " (* not ready *)";

      if (ps.generationId == 0)
      {
        ps.generationId = g->mGenerationId;
        ps.generationIdStr = std::to_string(ps.generationId);
        //printf("EMPTY => GENERATION %s\n",generationIdStr.c_str());
      }

      if (ps.generationId == g->mGenerationId)
      {
        if (g->mStatus != 1)
          ps.generationNotReady = true;

        ps.originTime = g->mAnalysisTime;
        ps.ostr1 << "<OPTION selected value=\"" <<  g->mGenerationId << "\">" <<  g->mName << status << "</OPTION>\n";
        session.setAttribute(ATTR_GENERATION_ID,ps.generationId);
      }
      else
        ps.ostr1 << "<OPTION value=\"" <<  g->mGenerationId << "\">" << g->mName << status << "</OPTION>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




void Plugin::page_main_generations(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Generations:

    ps.contentServer->getGenerationInfoListByProducerId(0,ps.producerId,ps.generationInfoList2);
    ps.generationInfoList2.getGenerationInfoListByProducerId(ps.producerId,ps.generationInfoList);
    //generationInfoList2.getGenerationInfoListByProducerIdAndStatus(producerId,generationInfoList,T::GenerationInfo::Status::Ready);

    ps.generationId = toUInt64(ps.generationIdStr);
    ps.generationNotReady = false;

    if (ps.generationInfoList.getGenerationInfoById(ps.generationId) == nullptr)
      ps.generationId = 0;

    getGenerations(ps.generationInfoList,ps.generations);


    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Generation:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";

    if (!ps.generations.empty())
    {
      std::string disabled;
      if (ps.generations.size() == 1)
        disabled = "disabled";

      ps.ostr1 << "<SELECT style=\"width:280px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_GENERATION_ID << "=' + this.options[this.selectedIndex].value)\">\n";

      for (auto it = ps.generations.rbegin(); it != ps.generations.rend(); ++it)
      {
        const std::string& name = *it;
        page_main_generationOption(ps,session,name);
      }
      ps.ostr1 << "</SELECT>\n";
    }
    ps.ostr1 << "</TD></TR>\n";

    if (ps.generationNotReady)
    {
      ps.ostr1 << "<TR style=\"text-align:center; font-size:12; font-weight:bold;\"><TD>*** Generation not ready ***</TD></TR>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The parameter selection. */

/*! \brief GridGui: The identifier and the descriptive name of a parameter key. */

void Plugin::page_main_parameterName(PageMainState& ps,const std::string& it,std::string& parameterId,std::string& pName)
{
  FUNCTION_TRACE
  try
  {
    if (strncasecmp(it.c_str(),"GRIB-",5) == 0)
    {
      std::string key = it.substr(5);

      Identification::GribParameterDef  def;
      if (Identification::gridDef.getGribParameterDefById(toUInt32(key),def))
      {
        pName = it + " (" + def.mParameterDescription + ")";
      }
    }
    else if (strncasecmp(it.c_str(),"NB-",3) == 0)
    {
      std::string key = it.substr(3);

      Identification::NewbaseParameterDef  def;
      if (Identification::gridDef.getNewbaseParameterDefById(toUInt32(key),def))
      {
        pName = it + " (" + def.mParameterName + ")";
      }
    }
    else if (strncasecmp(it.c_str(),"FMI-",4) == 0)
    {
      std::string key = it.substr(4);

      Identification::FmiParameterDef def;
      if (Identification::gridDef.getFmiParameterDefById(toUInt32(key),def))
      {
        parameterId = def.mParameterName;
        pName = def.mParameterName + " (" + def.mParameterDescription + ")";
        if (ps.parameterIdStr == parameterId || ps.parameterIdStr.empty())
          ps.unitStr = def.mParameterUnits;
      }
    }
    else
    {
      Identification::FmiParameterDef def;
      if (Identification::gridDef.getFmiParameterDefByName(it,def))
      {
        parameterId = def.mParameterName;
        pName = def.mParameterName + " (" + def.mParameterDescription + ")";
        if (ps.parameterIdStr == parameterId || ps.parameterIdStr.empty())
          ps.unitStr = def.mParameterUnits;
      }
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




void Plugin::page_main_parameters(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Parameters:

    ps.contentServer->getContentParamKeyListByGenerationId(0,ps.generationId,T::ParamKeyTypeValue::FMI_NAME,ps.paramKeyList);

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Parameter:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";

    if (!ps.paramKeyList.empty())
    {
      ps.ostr1 << R"(<SELECT style="width:280px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PARAMETER_ID << "=' + this.options[this.selectedIndex].value)\">\n";
      for (const auto& it : ps.paramKeyList)
      {
        std::string parameterId = it;
        std::string pName = it;
        page_main_parameterName(ps,it,parameterId,pName);


        if (ps.parameterIdStr.empty())
        {
          ps.parameterIdStr = parameterId;
          //printf("EMPTY => PARAMETER %s\n",parameterIdStr.c_str());
        }

        if (ps.parameterIdStr == parameterId)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  parameterId << "\">" <<  pName << "</OPTION>\n";
          session.setAttribute(ATTR_PARAMETER_ID,parameterId);
          ps.paramDescription = pName;
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  parameterId << "\">" <<  pName << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";
    }
    ps.ostr1 << "</TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The level type selection. */

void Plugin::page_main_levelIds(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Level identifiers:

    ps.contentServer->getContentListByParameterAndGenerationId(0,ps.generationId,T::ParamKeyTypeValue::FMI_NAME,ps.parameterIdStr,-1,0,0,-2,-2,-2,"14000101T000000","30000101T000000",0,ps.contentInfoList);
    ps.levelId = toInt32(ps.levelIdStr);

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Level type and value:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";

    getLevelIds(ps.contentInfoList,ps.levelIds);

    if (ps.levelIds.find(ps.levelId) == ps.levelIds.end())
      ps.levelId = -1;

    if (!ps.levelIds.empty())
    {
      std::string disabled;
      if (ps.levelIds.size() == 1)
        disabled = "disabled";

      ps.ostr1 << "<SELECT style=\"width:200px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LEVEL_ID << "=' + this.options[this.selectedIndex].value) \">\n";
      for (int it : ps.levelIds)
      {
        if (ps.levelIdStr.empty())
        {
          ps.levelIdStr = std::to_string(it);
          ps.levelId = it;
          //printf("EMPTY => LEVEL ID %s\n",levelIdStr.c_str());
        }

        std::string lStr = std::to_string(it);
        Identification::LevelDef levelDef;
        if (it < 1000)
        {
          if (Identification::gridDef.getFmiLevelDef(it,levelDef))
            lStr = "FMI-" + std::to_string(it) + " : " + levelDef.mDescription;
          else
            lStr = "FMI-" + std::to_string(it) + " : ";
        }
        if (ps.levelId == it)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  it << "\">" <<  lStr << "</OPTION>\n";
          session.setAttribute(ATTR_LEVEL_ID,ps.levelId);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  it << "\">" <<  lStr << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The level selection. */

void Plugin::page_main_levels(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Levels:

    ps.contentInfoList.clear();
    ps.contentServer->getContentListByParameterAndGenerationId(0,ps.generationId,T::ParamKeyTypeValue::FMI_NAME,ps.parameterIdStr,ps.levelId,0,0x7FFFFFFF,-2,-2,-2,"14000101T000000","30000101T000000",0,ps.contentInfoList);
    ps.level = toInt32(ps.levelStr);

    getLevels(ps.contentInfoList,ps.levelId,ps.levels);

    if (ps.levels.find(ps.level) == ps.levels.end())
      ps.level = 0;

    if (!ps.levels.empty())
    {
      std::string disabled;
      if (ps.levels.size() == 1)
        disabled = "disabled";

      ps.ostr1 << "<SELECT style=\"width:70px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LEVEL << "=' + this.options[this.selectedIndex].value)\">\n";
      for (int it : ps.levels)
      {
        if (ps.levelStr.empty())
        {
          ps.levelStr = std::to_string(it);
          ps.level = it;
          //printf("EMPTY => LEVEL %s\n",levelStr.c_str());
        }

        if (ps.level == it)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  it << "\">" <<  it << "</OPTION>\n";
          session.setAttribute(ATTR_LEVEL,ps.level);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  it << "\">" <<  it << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";
    }
    ps.ostr1 << "</TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The forecast type selection. */

void Plugin::page_main_forecastTypes(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Forecast type:

    ps.forecastType = toInt16(ps.forecastTypeStr);

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Forecast type and number:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";

    getForecastTypes(ps.contentInfoList,ps.levelId,ps.level,ps.forecastTypes);

    if (ps.forecastTypes.find(ps.forecastType) == ps.forecastTypes.end())
      ps.forecastType = 0;

    if (!ps.forecastTypes.empty())
    {
      std::string disabled;
      if (ps.forecastTypes.size() == 1)
        disabled = "disabled";

      ps.ostr1 << "<SELECT style=\"width:200px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_FORECAST_TYPE << "=' + this.options[this.selectedIndex].value)\">\n";
      for (int it : ps.forecastTypes)
      {
        if (ps.forecastTypeStr.empty())
        {
          ps.forecastTypeStr = std::to_string(it);
          ps.forecastType = it;
          //printf("EMPTY => FORECAST TYPE %s\n",forecastTypeStr.c_str());
        }

        std::string lStr = std::to_string(it);
        Identification::ForecastTypeDef typeDef;
        if (Identification::gridDef.getFmiForecastTypeDef(it,typeDef))
          lStr = std::to_string(it) + " : " + typeDef.mName;
        else
          lStr = std::to_string(it) + " : ";

        if (ps.forecastType == it)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  it << "\">" <<  lStr << "</OPTION>\n";
          session.setAttribute(ATTR_FORECAST_TYPE,ps.forecastType);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  it << "\">" << lStr << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The forecast number selection. */

void Plugin::page_main_forecastNumbers(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Forecast number:

    ps.forecastNumber = toInt16(ps.forecastNumberStr);

    getForecastNumbers(ps.contentInfoList,ps.levelId,ps.level,ps.forecastType,ps.forecastNumbers);

    if (ps.forecastNumbers.find(ps.forecastNumber) == ps.forecastNumbers.end())
      ps.forecastNumber = 0;

    if (!ps.forecastNumbers.empty())
    {
      std::string disabled;
      if (ps.forecastNumbers.size() == 1)
        disabled = "disabled";

      ps.ostr1 << "<SELECT style=\"width:70px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_FORECAST_NUMBER << "=' + this.options[this.selectedIndex].value)\">\n";
      for (int it : ps.forecastNumbers)
      {
        if (ps.forecastNumberStr.empty())
        {
          ps.forecastNumberStr = std::to_string(it);
          ps.forecastNumber = it;
          //printf("EMPTY => FORECAST NUMBER %s\n",forecastNumberStr.c_str());
        }

        if (it == ps.forecastNumber)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  it << "\">" <<  it << "</OPTION>\n";
          session.setAttribute(ATTR_FORECAST_NUMBER,ps.forecastNumber);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  it << "\">" <<  it << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";
    }
    ps.ostr1 << "</TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The geometry selection. */

void Plugin::page_main_geometries(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Geometries:

    ps.geometryId = toInt32(ps.geometryIdStr);

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Geometry:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";

    getGeometries(ps.contentInfoList,ps.levelId,ps.level,ps.forecastType,ps.forecastNumber,ps.geometries);

    if (ps.geometries.find(ps.geometryId) == ps.geometries.end())
      ps.geometryId = 0;

    if (!ps.geometries.empty())
    {
      std::string disabled;
      if (ps.geometries.size() == 1)
        disabled = "disabled";

      ps.ostr1 << "<SELECT style=\"width:280px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_GEOMETRY_ID << "=' + this.options[this.selectedIndex].value)\">\n";

      for (int geometrie : ps.geometries)
      {
        std::string st = std::to_string(geometrie);

        std::string gName;
        uint cols = 0;
        uint rows = 0;

        Identification::gridDef.getGeometryNameById(geometrie,gName);

        if (Identification::gridDef.getGridDimensionsByGeometryId(geometrie,cols,rows))
          st = std::to_string(geometrie) + ":" + gName + " (" + std::to_string(cols) + " x " + std::to_string(rows) + ")";
        else
          st = std::to_string(geometrie) + ":" + gName;

        if (ps.geometryId == 0)
        {
          ps.geometryId = geometrie;
          ps.geometryIdStr = std::to_string(ps.geometryId);
          //printf("EMPTY => GEOMETRY %s\n",geometryIdStr.c_str());
        }

        if (ps.geometryId == geometrie)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  geometrie << "\">" <<  st << "</OPTION>\n";
          session.setAttribute(ATTR_GEOMETRY_ID,ps.geometryId);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  geometrie << "\">" <<  st << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";
    }
    ps.ostr1 << "</TD></TR>\n";


    if (ps.geometryId != 0)
    {
      std::string tmp = ps.producerNameStr + ":" + std::to_string(ps.geometryId);
      if (itsProducerList.find(toUpperString(tmp)) == itsProducerList.end())
      {
        ps.ostr1 << "<TR style=\"text-align:center; font-size:12; font-weight:bold;\"><TD >*** Search not configured ***</TD></TR>\n";
      }
    }


    if (ps.projectionIdStr.empty())
    {
      ps.projectionIdStr = ps.geometryIdStr;
      session.setAttribute(ATTR_PROJECTION_ID,ps.projectionIdStr);
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The time selection. */

/*! \brief GridGui: The time group selection. */

/*! \brief GridGui: Collect the time groups of the selected geometry, forecast type and number. */

void Plugin::page_main_collectTimeGroups(PageMainState& ps)
{
  FUNCTION_TRACE
  try
  {
    if (ps.timeGroupType > 0)
    {
      for (uint a=0; a<ps.len; a++)
      {
        T::ContentInfo *g = ps.contentInfoList.getContentInfoByIndex(a);

        if (g->mGeometryId == ps.geometryId)
        {
          if (ps.prevTime < g->getForecastTime())
          {
            if (ps.forecastType == g->mForecastType)
            {
              if (ps.forecastNumber == g->mForecastNumber)
              {
                std::string ft = g->getForecastTime();
                ps.timeGroupStrList.insert(ft.substr(0,timeGroupLen[ps.timeGroupType]));
                ps.tCount++;
              }
            }
          }
        }
      }
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




void Plugin::page_main_timeGroups(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Times:

    ps.contentInfoList.clear();
    ps.contentServer->getContentListByParameterAndGenerationId(0,ps.generationId,T::ParamKeyTypeValue::FMI_NAME,ps.parameterIdStr,ps.levelId,ps.level,ps.level,-2,-2,-2,"14000101T000000","30000101T000000",0,ps.contentInfoList);
    ps.len = ps.contentInfoList.getLength();

    ps.pTime = ps.timeStr;

    ps.timeGroupType = 0;
    ps.useTimeGroup = false;


    ps.ostr1 << "<TR height=\"15\"><TD><HR/></TD></TR>\n";
    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Time group:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD><TABLE><TR><TD>\n";

    ps.ostr1 << R"(<SELECT style="width:80px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_TIME_GROUP << "=&" << ATTR_TIME_GROUP_TYPE << "=' + this.options[this.selectedIndex].value)\">\n";

    ps.a = 0;
    while (timeGroupTypes[ps.a] != nullptr)
    {
      if (ps.timeGroupTypeStr == timeGroupTypes[ps.a])
      {
        ps.timeGroupType = ps.a;
        ps.ostr1 << "<OPTION selected value=\"" << timeGroupTypes[ps.a] << "\">" <<  timeGroupTypes[ps.a] << "</OPTION>\n";
        session.setAttribute(ATTR_TIME_GROUP_TYPE,ps.timeGroupTypeStr);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  timeGroupTypes[ps.a] << "\">" <<  timeGroupTypes[ps.a] << "</OPTION>\n";
      }
      ps.a++;
    }
    ps.ostr1 << "</SELECT></TD>\n";

    if (ps.timeStr.empty() &&  ps.len > 0)
    {
      T::ContentInfo *g = ps.contentInfoList.getContentInfoByIndex(0);
      std::string ft = g->getForecastTime();
      if (ps.timeGroupStr.empty())
        ps.timeGroupStr = ft.substr(0,timeGroupLen[ps.timeGroupType]);
    }

    ps.prevTime = "14000101T0000";
    ps.tCount = 0;

    page_main_collectTimeGroups(ps);

    if (ps.timeGroupStrList.size() > 1)
    {
      ps.useTimeGroup = true;

      ps.ostr1 << R"(<TD><SELECT id="yearselect" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() + "' + this.options[this.selectedIndex].value)\"";
      ps.ostr1 << " >\n";

      for (const auto& it : ps.timeGroupStrList)
      {
        std::ostringstream out;
        out << "&" << ATTR_TIME_GROUP << "=" << it << "&" << ATTR_FORECAST_TYPE << "=" << ps.forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << ps.forecastNumberStr;
        std::string url = out.str();

        if (it == ps.timeGroupStr)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  url << "\">" <<  it << "</OPTION>\n";
          session.setAttribute(ATTR_TIME_GROUP,ps.timeGroupStr);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  url << "\">" <<  it << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT></TD>\n";
    }
    ps.ostr1 << "</TR></TABLE></TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The time selection and the time bar. */

/*! \brief GridGui: The option and the time bar cell of one time. */

void Plugin::page_main_timeOption(PageMainState& ps,Session& session,T::ContentInfo *g,const std::string& u,uint& daySwitch,uint& cc)
{
  FUNCTION_TRACE
  try
  {
    std::ostringstream out;
    out << "&" << ATTR_TIME << "=" << g->getForecastTime() << "&" << ATTR_FILE_ID << "=" << g->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << ps.forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << ps.forecastNumberStr;
    std::string url = out.str();
    const std::string& uu = url;

    if (ps.currentCont != nullptr  &&  ps.nextCont == nullptr)
      ps.nextCont = g;

    if (ps.timeStr.empty())
    {
      ps.timeStr = g->getForecastTime();
      ps.prevTime = ps.timeStr;
      //printf("EMPTY => TIME %s\n",timeStr.c_str());
    }

    std::string bg = "#E0E0E0";

    if (strncmp(g->getForecastTime(),ps.pTime.c_str(),8) != 0)
      daySwitch++;

    if ((daySwitch % 2) == 1)
      bg = "#D0D0D0";

    if (ps.timeStr == g->getForecastTime())
      bg = "#0000FF";

    if (ps.tCount < 124  ||  (g->getForecastTime() >= ps.timeStr  &&  cc < 124))
    {
      if (cc == 0)
      {
        ps.ostr3 << "<TD style=\"text-align:center; font-size:12;width:30;background:#000000;color:#FFFFFF;\">UTC</TD>\n";
        ps.ostr3 << R"(<TD style="text-align:center; font-size:12;width:120;background:#F0F0F0;" id="ftime">)" + ps.timeStr + "</TD><TD style=\"width:1;\"> </TD>\n";
      }

      if (u > " ")
      {
        ps.ostr3 << "<TD style=\"width:5; background:" << bg << ";\" ";
        ps.ostr3 << " onmouseout=\"this.style='width:5;background:"<< bg << ";'\"";
        ps.ostr3 << " onmouseover=\"this.style='width:5;height:30;background:#FF0000;'; setText('ftime','" << g->getForecastTime() << "');setText('flevel','" << g->mParameterLevel << "');setImage(document.getElementById('myimage'),'" << u << uu << "');\"";
        ps.ostr3 << " onClick=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << ";" << ATTR_TIME << "=" << g->getForecastTime() << ";" << ATTR_FILE_ID << "=" << g->mFileId << ";" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << ";" << ATTR_FORECAST_TYPE << "=" << g->mForecastType << ";" << ATTR_FORECAST_NUMBER << "=" << g->mForecastNumber << "');\" > </TD>\n";

      }
      else
        ps.ostr3 << "<TD style=\"width:5; background:"+bg+";\"> </TD>\n";

      ps.prevTime = g->getForecastTime();
      cc++;
    }

    if (ps.timeStr == g->getForecastTime())
    {
      //printf("## SELECTED TIME  [%s][%s]\n",timeStr.c_str(),g->getForecastTime());
      ps.ostr1 << "<OPTION selected value=\"" <<  url << "\">" <<  g->getForecastTime() << "</OPTION>\n";
      ps.currentCont = g;
      ps.fmiKeyStr = getFmiKey(ps.producerNameStr,*g);
      ps.fileIdStr = std::to_string(g->mFileId);
      ps.messageIndexStr = std::to_string(g->mMessageIndex);

      session.setAttribute(ATTR_FMI_KEY,ps.fmiKeyStr);
      session.setAttribute(ATTR_FILE_ID,g->mFileId);
      session.setAttribute(ATTR_MESSAGE_INDEX,g->mMessageIndex);
      session.setAttribute(ATTR_TIME,g->getForecastTime());
    }
    else
    {
      ps.ostr1 << "<OPTION value=\"" <<  url << "\">" <<  g->getForecastTime() << "</OPTION>\n";
    }

    if (ps.currentCont == nullptr)
      ps.prevCont = g;

    ps.pTime = g->getForecastTime();
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




void Plugin::page_main_timeSelection(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    ps.prevTime = "14000101T0000";

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Time (UTC):</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD><TABLE><TR><TD>\n";

    ps.ostr3 << "<TABLE style=\"border-width:0;border-spacing:0;height:30;\"><TR>\n";


    ps.prevCont = nullptr;
    ps.currentCont = nullptr;
    ps.nextCont = nullptr;

    if (ps.len > 0)
    {
      ps.contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiName_producer_generation_level_time);

      ps.ostr1 << R"(<SELECT id="timeselect" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() + "' + this.options[this.selectedIndex].value)\">\n";

      std::string u;
      if (ps.presentation == "Image" ||  ps.presentation == "Map"  ||  ps.presentation == "Streams")
      {
        u = "/grid-gui?session=" + session.getUrlParameter() + "&" + ATTR_PAGE + "=" + ps.presentation;
      }

      uint daySwitch = 0;
      uint cc = 0;
      for (uint a=0; a<ps.len; a++)
      {
        T::ContentInfo *g = ps.contentInfoList.getContentInfoByIndex(a);

        //printf("**** TIME %s (%s)  geom=%u %u  ft=%u %u   fn=%d %d \n",g->getForecastTime(),prevTime.c_str(),g->mGeometryId,geometryId,forecastType,g->mForecastType,forecastNumber,g->mForecastNumber);

        if (isSelectableTime(ps,*g))
          page_main_timeOption(ps,session,g,u,daySwitch,cc);
      }
      ps.ostr1 << "</SELECT>\n";
    }
    ps.ostr1 << "</TD>\n";

    if (ps.prevCont != nullptr)
      ps.ostr1 << R"(<TD width="20" > <button type="button" onClick="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_TIME << "=" << ps.prevCont->getForecastTime() << "&" << ATTR_FILE_ID << "=" << ps.prevCont->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << ps.prevCont->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << ps.forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << ps.forecastNumberStr << "');\">&lt;</button></TD>\n";
    else
      ps.ostr1 << "<TD width=\"20\"><button type=\"button\">&lt;</button></TD>\n";

    if (ps.nextCont != nullptr)
      ps.ostr1 << R"(<TD width="20"><button type="button" onClick="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_TIME << "=" << ps.nextCont->getForecastTime() << "&" << ATTR_FILE_ID << "=" << ps.nextCont->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << ps.nextCont->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << ps.forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << ps.forecastNumberStr + "');\">&gt;</button></TD>\n";
    else
      ps.ostr1 << "<TD width=\"20\"><button type=\"button\">&gt;</button></TD>\n";

    ps.ostr1 << "</TR></TABLE></TD></TR>\n";

    ps.ostr3 << "<TD></TD></TR></TABLE>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The level bar and the FMI key. */

void Plugin::page_main_levelBar(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    if (!ps.timeStr.empty())
      ps.contentServer->getContentListByParameterAndGenerationId(0,ps.generationId,T::ParamKeyTypeValue::FMI_NAME,ps.parameterIdStr,ps.levelId,0,1000000000,ps.forecastType,ps.forecastNumber,ps.geometryId,ps.timeStr,ps.timeStr,0,ps.contentInfoListByLevels);

    ps.lCount = ps.contentInfoListByLevels.getLength();

    std::string u;
    if (ps.presentation == "Image" ||  ps.presentation == "Map" ||  ps.presentation == "Streams")
    {
      u = "/grid-gui?session=" + session.getUrlParameter() + "&" + ATTR_PAGE + "=" + ps.presentation;
    }

    if (itsAnimationEnabled)
    {
      ps.ostr4 << "<TABLE style=\"border-width:0;border-spacing:0;width:70;\">\n";
      ps.ostr4 << "<TR><TD style=\"height:35;\"> </TD></TR>\n";
      ps.ostr4 << "<TR><TD style=\"height:35;text-align:center; font-size:12;background:#000000;color:#FFFFFF;\">Level</TD></TR>\n";
      ps.ostr4 << R"(<TR><TD style="text-align:center; font-size:12;background:#F0F0F0;" id="flevel">)" << ps.levelStr << "</TD></TR>\n";

      for (uint a=0; a<ps.lCount; a++)
      {
        T::ContentInfo *g = ps.contentInfoListByLevels.getContentInfoByIndex(a);

        std::ostringstream out;
        out << "&" << ATTR_TIME << "=" << ps.timeStr << "&" << ATTR_FILE_ID << "=" << g->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << ps.forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << ps.forecastNumberStr;
        std::string url = out.str();
        const std::string& uu = url;

        std::string bg = "#E0E0E0";
        if (g->mParameterLevel == ps.level)
          bg = "#0000FF";

        if (u > " ")
        {
          ps.ostr4 << R"(<TR style="height:5;"><TD style=" background:)" << bg << ";\"";
          ps.ostr4 << " onmouseout=\"this.style='background:"+bg+";'\"";
          ps.ostr4 << " onmouseover=\"this.style='background:#FF0000;';setText('ftime','" << g->getForecastTime() << "');setText('flevel','" << g->mParameterLevel << "');setImage(document.getElementById('myimage'),'" << u << uu << "');\"";
          ps.ostr4 << " onClick=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << ";" << ATTR_TIME << "=" << g->getForecastTime() << ";" << ATTR_FILE_ID << "=" << g->mFileId << ";" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << ";" << ATTR_FORECAST_TYPE << "=" << g->mForecastType << ";" << ATTR_FORECAST_NUMBER << "=" << g->mForecastNumber << ";" << ATTR_LEVEL << "=" << g->mParameterLevel << "');\"> </TD></TR>\n";
        }
        else
          ps.ostr4 << R"(<TR style="height:5;"><TD style=" background:)" << bg <<";\"> </TD></TR>\n";
      }
      ps.ostr4 << "</TABLE>\n";
    }

    ps.ostr1 << "<TR height=\"15\"><TD><HR/></TD></TR>\n";
    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12; width:100%;\"><TD>FMI Key:</TD></TR>\n";
    ps.ostr1 << R"(<TR height="30"><TD><INPUT type="text" style="width:280px;background-color:#FFFF00;" value=")" << ps.fmiKeyStr << "\"></TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




void Plugin::page_main_times(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    page_main_timeGroups(ps,session);



    page_main_timeSelection(ps,session);



    page_main_levelBar(ps,session);
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The presentation selection. */

void Plugin::page_main_presentation(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Presentation:


    ps.ostr1 << "<TR height=\"15\"><TD><HR/></TD></TR>\n";
    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Presentation:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";
    ps.ostr1 << R"(<SELECT style="width:280px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_TIME << "=" << ps.timeStr << "&" << ATTR_FILE_ID << "=" << ps.fileIdStr << "&" << ATTR_MESSAGE_INDEX << "=" << ps.messageIndexStr << "&" << ATTR_FORECAST_TYPE << "=" << ps.forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << ps.forecastNumberStr << "&" << ATTR_PRESENTATION << "=' + this.options[this.selectedIndex].value)\">\n";

    ps.a = 0;
    while (modes[ps.a] != nullptr)
    {
      if (ps.presentation.empty())
      {
        ps.presentation = modes[ps.a];
        //printf("EMPTY => PRESENTATION %s\n",presentation.c_str());
      }

      if (ps.presentation == modes[ps.a])
      {
        ps.ostr1 << "<OPTION selected value=\"" <<  modes[ps.a] << "\">" <<  modes[ps.a] << "</OPTION>\n";
        session.setAttribute(ATTR_PRESENTATION,ps.presentation);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  modes[ps.a] << "\">" <<  modes[ps.a] << "</OPTION>\n";
      }

      ps.a++;
    }


    ps.ostr1 << "</SELECT>\n";
    ps.ostr1 << "</TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The projection selection. */

/*! \brief GridGui: The projection lock selection. */

void Plugin::page_main_projectionLock(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    const char *projectionLockTypes[] = {"","Lock",nullptr};

    ps.ostr1 << R"(<SELECT style="width:70px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PROJECTION_LOCK << "=' + this.options[this.selectedIndex].value)\">\n";

    uint a = 0;
    while (projectionLockTypes[a] != nullptr)
    {
      if (ps.projectionLockStr == projectionLockTypes[a])
      {
        ps.ostr1 << "<OPTION selected value=\"" << projectionLockTypes[a] << "\">" <<  projectionLockTypes[a] << "</OPTION>\n";
        session.setAttribute(ATTR_PROJECTION_LOCK,projectionLockTypes[a]);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  projectionLockTypes[a] << "\">" <<  projectionLockTypes[a] << "</OPTION>\n";
      }
      a++;
    }
    ps.ostr1 << "</SELECT>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The label of a projection in the projection selection. */

void Plugin::page_main_projectionLabel(int projection,std::string& st)
{
  FUNCTION_TRACE
  try
  {
    std::string gName;
    uint cols = 0;
    uint rows = 0;

    Identification::gridDef.getGeometryNameById(projection,gName);

    if (Identification::gridDef.getGridDimensionsByGeometryId(projection,cols,rows))
      st = std::to_string(projection) + ":" + gName + " (" + std::to_string(cols) + " x " + std::to_string(rows) + ")";
    else
      st = std::to_string(projection) + ":" + gName;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The option of one projection in the projection selection. */

void Plugin::page_main_projectionOption(PageMainState& ps,Session& session,int projection,const std::string& st,T::GeometryId& projectionId)
{
  FUNCTION_TRACE
  try
  {
    if (projectionId == 0)
    {
      projectionId = projection;
      ps.projectionIdStr = std::to_string(projectionId);
      //printf("EMPTY => PROJECTION %s\n",projectionIdStr.c_str());
    }

    if (projectionId == projection)
    {
      ps.ostr1 << "<OPTION selected value=\"" <<  projection << "\">" <<  st << "</OPTION>\n";
      session.setAttribute(ATTR_PROJECTION_ID,projectionId);
    }
    else
    {
      ps.ostr1 << "<OPTION value=\"" <<  projection << "\">" <<  st << "</OPTION>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




void Plugin::page_main_projections(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    if (ps.presentation == "Image" || ps.presentation == "Streams" || ps.presentation == "StreamsAnimation")
    {
      // ### Projections:

      std::set<T::GeometryId> projections;
      Identification::gridDef.getGeometryIdList(projections);

      T::GeometryId projectionId  = toInt32(ps.projectionIdStr);

      ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Projection:</TD></TR>\n";
      ps.ostr1 << "<TR height=\"30\"><TD>\n";

      if (projectionId == 0)
      {
        projectionId = ps.geometryId;
      }

      if (projections.find(projectionId) == projections.end())
        projectionId = ps.geometryId;

      if (!projections.empty())
      {
        ps.ostr1 << R"(<SELECT style="width:200px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PROJECTION_ID << "=' + this.options[this.selectedIndex].value)\">\n";

        for (int projection : projections)
        {
          std::string st = std::to_string(projection);

          if (projectionId == projection || itsBlockedProjections.find(projection) == itsBlockedProjections.end())
          {
            page_main_projectionLabel(projection,st);
            page_main_projectionOption(ps,session,projection,st,projectionId);
          }
        }
        ps.ostr1 << "</SELECT>\n";
      }

      page_main_projectionLock(ps,session);

      ps.ostr1 << "</TD></TR>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The color map selection. */

void Plugin::page_main_colorMaps(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    if (ps.presentation == "Image" ||  ps.presentation == "Map")
    {
      // ### Color maps:

      std::set<std::string> names;

      for (auto& itsColorMapFile : itsColorMapFiles)
      {
        string_vec nameList = itsColorMapFile.getNames();
        for (auto& name : nameList)
          names.insert(name);
      }

      ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Color map and opacity:</TD></TR>\n";
      ps.ostr1 << "<TR height=\"30\"><TD>\n";
      ps.ostr1 << R"(<SELECT style="width:220px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_COLOR_MAP << "=' + this.options[this.selectedIndex].value)\">\n";

      if (ps.colorMap.empty() ||  ps.colorMap == "None")
      {
        ps.ostr1 << "<OPTION selected value=\"None\">None</OPTION>\n";
        session.setAttribute(ATTR_COLOR_MAP,"None");
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"None\">None</OPTION>\n";
      }

      for (const auto& name : names)
      {
        if (ps.colorMap == name)
        {
          ps.ostr1 << "<OPTION selected value=\"" << name << "\">" <<  name << "</OPTION>\n";
          session.setAttribute(ATTR_COLOR_MAP,ps.colorMap);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  name << "\">" <<  name << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";

      ps.ostr1 << R"(<SELECT style="width:50px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_OPACITY << "=' + this.options[this.selectedIndex].value)\">\n";


      for (uint a=0; a<256; a++)
      {
        if (a == ps.opacity)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          session.setAttribute(ATTR_OPACITY,a);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";

      ps.ostr1 << "</TD></TR>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The paint options of the presentation. */

/*! \brief GridGui: The hue, saturation and blur selections. */

void Plugin::page_main_hueOptions(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    if ((ps.colorMap.empty() || ps.colorMap == "None") &&  ps.presentation != "Streams" && ps.presentation != "StreamsAnimation")
    {
      // ### Hue, saturation, blur

      ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Hue, saturation and blur</TD></TR>\n";
      ps.ostr1 << "<TR height=\"30\"><TD>\n";
      ps.ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_HUE << "=' + this.options[this.selectedIndex].value)\">\n";


      uint hue = toUInt32(ps.hueStr);
      for (uint a=0; a<256; a++)
      {
        if (a == hue)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          session.setAttribute(ATTR_HUE,a);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";


      uint saturation = toUInt32(ps.saturationStr);
      ps.ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SATURATION << "=' + this.options[this.selectedIndex].value)\">\n";


      for (uint a=0; a<256; a++)
      {
        if (a == saturation)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          session.setAttribute(ATTR_SATURATION,a);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";


      ps.ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_BLUR << "=' + this.options[this.selectedIndex].value)\">\n";

      uint blur = toUInt32(ps.blurStr);
      for (uint a=1; a<=200; a++)
      {
        if (a == blur)
        {
          ps.ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          session.setAttribute(ATTR_BLUR,a);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        }
      }
      ps.ostr1 << "</SELECT>\n";
      ps.ostr1 << "</TD></TR>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The stream line selections. */

void Plugin::page_main_streamOptions(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    if (ps.presentation == "Streams" || ps.presentation == "StreamsAnimation")
    {
      // ### step, minLength, maxLength, background

      ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Step, min and max length, color</TD></TR>\n";
      ps.ostr1 << "<TR height=\"30\"><TD>\n";
      ps.ostr1 << R"(<SELECT style="width:40px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_STEP << "=' + this.options[this.selectedIndex].value)\">\n";

      uint step = toUInt32(ps.stepStr);
      writeNumberOptions(ps.ostr1,session,ATTR_STEP,step,2,100,2);
      ps.ostr1 << "</SELECT>\n";


      uint minLength = toUInt32(ps.minLengthStr);
      ps.ostr1 << R"(<SELECT style="width:50px;"onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_MIN_LENGTH << "=' + this.options[this.selectedIndex].value)\">\n";

      writeNumberOptions(ps.ostr1,session,ATTR_MIN_LENGTH,minLength,2,128,2);
      writeNumberOptions(ps.ostr1,session,ATTR_MIN_LENGTH,minLength,128,2049,64);
      ps.ostr1 << "</SELECT>\n";


      ps.ostr1 << R"(<SELECT style="width:50px;"onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_MAX_LENGTH << "=' + this.options[this.selectedIndex].value)\">\n";

      uint maxLength = toUInt32(ps.maxLengthStr);
      writeNumberOptions(ps.ostr1,session,ATTR_MAX_LENGTH,maxLength,8,128,4);
      writeNumberOptions(ps.ostr1,session,ATTR_MAX_LENGTH,maxLength,128,2049,64);
      ps.ostr1 << "</SELECT>\n";

      ps.ostr1 << R"(<SELECT style="width:120px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_STREAM_COLOR << "=' + this.options[this.selectedIndex].value)\">\n";

      for (auto& itsColor : itsColors)
      {
        if (ps.streamColorStr == itsColor.first)
        {
          ps.ostr1 << "<OPTION selected value=\"" << itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
          session.setAttribute(ATTR_STREAM_COLOR,ps.streamColorStr);
        }
        else
        {
          ps.ostr1 << "<OPTION value=\"" <<  itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
        }
        ps.a++;
      }
      ps.ostr1 << "</SELECT>\n";
      ps.ostr1 << "</TD></TR>\n";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The coordinate line color selection. */

void Plugin::page_main_coordinateLineOptions(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Coordinate lines:

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Coordinate lines and land border:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";
    ps.ostr1 << R"(<SELECT style="width:280px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_COORDINATE_LINES << "=' + this.options[this.selectedIndex].value)\">\n";

    for (auto& itsColor : itsColors)
    {
      if (ps.coordinateLinesStr == itsColor.first)
      {
        ps.ostr1 << "<OPTION selected value=\"" << itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
        session.setAttribute(ATTR_COORDINATE_LINES,ps.coordinateLinesStr);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
      }

      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The land border color selection. */

void Plugin::page_main_landBorderOptions(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Land border:

    ps.ostr1 << R"(<SELECT style="width:280px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_LAND_BORDER << "=' + this.options[this.selectedIndex].value)\">\n";

    for (auto& itsColor : itsColors)
    {
      if (ps.landBorderStr == itsColor.first)
      {
        ps.ostr1 << "<OPTION selected value=\"" << itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
        session.setAttribute(ATTR_LAND_BORDER,ps.landBorderStr);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
      }
      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";
    ps.ostr1 << "</TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The land and sea color selections. */

void Plugin::page_main_landSeaColorOptions(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Land and sea colors:

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Land and sea colors:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";

    ps.a = 0;
    ps.ostr1 << R"(<SELECT style="width:80px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_LAND_COLOR_POS << "=' + this.options[this.selectedIndex].value)\">\n";
    while (layerPosition[ps.a] != nullptr)
    {
      if (ps.landcol_pos == ps.a)
      {
        ps.ostr1 << "<OPTION selected value=\"" << ps.a << "\">" <<  layerPosition[ps.a] << "</OPTION>\n";
        session.setAttribute(ATTR_LAND_COLOR_POS,ps.a);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  ps.a << "\">" << layerPosition[ps.a] << "</OPTION>\n";
      }
      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";


    ps.a = 0;
    ps.ostr1 << R"(<SELECT style="width:190px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_LAND_MASK << "=' + this.options[this.selectedIndex].value)\">\n";
    for (auto& itsColor : itsColors)
    {
      if (ps.landMaskStr == itsColor.first)
      {
        ps.ostr1 << "<OPTION selected value=\"" << itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
        session.setAttribute(ATTR_LAND_MASK,ps.landMaskStr);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
      }
      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";


    ps.a = 0;
    ps.ostr1 << R"(<SELECT style="width:80px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_SEA_COLOR_POS << "=' + this.options[this.selectedIndex].value)\">\n";
    while (layerPosition[ps.a] != nullptr)
    {
      if (ps.seacol_pos == ps.a)
      {
        ps.ostr1 << "<OPTION selected value=\"" << ps.a << "\">" <<  layerPosition[ps.a] << "</OPTION>\n";
        session.setAttribute(ATTR_SEA_COLOR_POS,ps.a);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  ps.a << "\">" << layerPosition[ps.a] << "</OPTION>\n";
      }
      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";


    ps.ostr1 << R"(<SELECT style="width:190px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_SEA_MASK << "=' + this.options[this.selectedIndex].value)\">\n";
    ps.a = 0;
    for (auto& itsColor : itsColors)
    {
      if (ps.seaMaskStr == itsColor.first)
      {
        ps.ostr1 << "<OPTION selected value=\"" << itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
        session.setAttribute(ATTR_SEA_MASK,ps.seaMaskStr);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  itsColor.first << "\">" <<  itsColor.first << "</OPTION>\n";
      }
      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";

    ps.ostr1 << "</TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The land and sea shading selections. */

void Plugin::page_main_topologyOptions(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Land and sea topology

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Land/sea topography positions and intensity:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";

    ps.a = 0;
    ps.ostr1 << R"(<SELECT style="width:80px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_LAND_SHADING_POS << "=' + this.options[this.selectedIndex].value)\">\n";
    while (layerPosition[ps.a] != nullptr)
    {
      if (ps.landshade_pos == ps.a)
      {
        ps.ostr1 << "<OPTION selected value=\"" << ps.a << "\">" <<  layerPosition[ps.a] << "</OPTION>\n";
        session.setAttribute(ATTR_LAND_SHADING_POS,ps.a);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  ps.a << "\">" << layerPosition[ps.a] << "</OPTION>\n";
      }

      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";


    ps.ostr1 << R"(<SELECT style="width:55px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_LAND_SHADING_LIGHT << "=' + this.options[this.selectedIndex].value)\">\n";

    for (uint t=0; t<=512; t=t+16)
    {
      if (ps.landshade_light == t)
      {
        ps.ostr1 << "<OPTION selected value=\"" << t << "\">" <<  t << "</OPTION>\n";
        session.setAttribute(ATTR_LAND_SHADING_LIGHT,t);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  t << "\">" << t << "</OPTION>\n";
      }

      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";

    ps.ostr1 << R"(<SELECT style="width:55px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_LAND_SHADING_SHADOW << "=' + this.options[this.selectedIndex].value)\">\n";

    for (uint t=0; t<=512; t=t+16)
    {
      if (ps.landshade_shadow == t)
      {
        ps.ostr1 << "<OPTION selected value=\"" << t << "\">" <<  t << "</OPTION>\n";
        session.setAttribute(ATTR_LAND_SHADING_SHADOW,t);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  t << "\">" << t << "</OPTION>\n";
      }

      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";

    ps.ostr1 << "</TD></TR>\n";
    ps.ostr1 << "<TR><TD>\n";

    ps.a = 0;
    ps.ostr1 << R"(<SELECT style="width:80px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_SEA_SHADING_POS << "=' + this.options[this.selectedIndex].value)\">\n";
    while (layerPosition[ps.a] != nullptr)
    {
      if (ps.seashade_pos == ps.a)
      {
        ps.ostr1 << "<OPTION selected value=\"" << ps.a << "\">" <<  layerPosition[ps.a] << "</OPTION>\n";
        session.setAttribute(ATTR_SEA_SHADING_POS,ps.a);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  ps.a << "\">" << layerPosition[ps.a] << "</OPTION>\n";
      }

      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";


    ps.ostr1 << R"(<SELECT style="width:55px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_SEA_SHADING_LIGHT << "=' + this.options[this.selectedIndex].value)\">\n";

    for (uint t=0; t<=512; t=t+16)
    {
      if (ps.seashade_light == t)
      {
        ps.ostr1 << "<OPTION selected value=\"" << t << "\">" <<  t << "</OPTION>\n";
        session.setAttribute(ATTR_SEA_SHADING_LIGHT,t);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  t << "\">" << t << "</OPTION>\n";
      }

      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";


    ps.ostr1 << R"(<SELECT style="width:55px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_SEA_SHADING_SHADOW << "=' + this.options[this.selectedIndex].value)\">\n";

    for (uint t=0; t<=512; t=t+16)
    {
      if (ps.seashade_shadow == t)
      {
        ps.ostr1 << "<OPTION selected value=\"" << t << "\">" <<  t << "</OPTION>\n";
        session.setAttribute(ATTR_SEA_SHADING_SHADOW,t);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  t << "\">" << t << "</OPTION>\n";
      }

      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";


    ps.ostr1 << "</TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*! \brief GridGui: The missing value selection and the value field. */

void Plugin::page_main_missingValueOptions(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    // ### Missing value:

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Missing Value:</TD></TR>\n";
    ps.ostr1 << "<TR height=\"30\"><TD>\n";
    ps.ostr1 << R"(<SELECT style="width:280px;" onchange="getPage(this,parent,'/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_MISSING << "=' + this.options[this.selectedIndex].value)\">\n";

    const char *missingValues[] = {"Default","Zero",nullptr};

    ps.a = 0 ;
    while (missingValues[ps.a] != nullptr)
    {
      if (ps.missingStr == missingValues[ps.a])
      {
        ps.ostr1 << "<OPTION selected value=\"" << missingValues[ps.a] << "\">" <<  missingValues[ps.a] << "</OPTION>\n";
        session.setAttribute(ATTR_MISSING,ps.missingStr);
      }
      else
      {
        ps.ostr1 << "<OPTION value=\"" <<  missingValues[ps.a] << "\">" <<  missingValues[ps.a] << "</OPTION>\n";
      }
      ps.a++;
    }
    ps.ostr1 << "</SELECT>\n";
    ps.ostr1 << "</TD></TR>\n";


    ps.ostr1 << "<TR height=\"15\"><TD><HR/></TD></TR>\n";

    // ## Value and units:

    ps.ostr1 << "<TR height=\"15\" style=\"font-size:12; width:100%;\"><TD>Value and units:</TD></TR>\n";
    ps.ostr1 << R"(<TR height="30"><TD><INPUT style="width:200px;" type="text" id="gridValue"><INPUT style="width:80px;"type="text" value=")" << ps.unitStr << "\"></TD></TR>\n";
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




void Plugin::page_main_paintOptions(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    if (ps.presentation == "Image" || ps.presentation == "Map" || ps.presentation == "Streams" || ps.presentation == "StreamsAnimation")
    {
      page_main_hueOptions(ps,session);
      page_main_streamOptions(ps,session);
      page_main_coordinateLineOptions(ps,session);
      page_main_landBorderOptions(ps,session);
      page_main_landSeaColorOptions(ps,session);
      page_main_topologyOptions(ps,session);
      page_main_missingValueOptions(ps,session);
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}



/*! \brief GridGui: The image frame and the animation of the presentation. */

void Plugin::page_main_imageFrame(PageMainState& ps,Session& session)
{
  FUNCTION_TRACE
  try
  {
    ps.ostr1 << "<TR height=\"50%\"><TD> </TD></TR>\n";

    // ## Download
    ps.ostr1 << R"(<TR height="30" style="font-size:16; font-weight:bold; width:280px; color:#000000; background:#D0D0D0; vertical-align:middle; text-align:center; "><TD><a href="grid-gui?)" << ATTR_PAGE << "=download&" << ATTR_FILE_ID << "=" << ps.fileIdStr << "&" << ATTR_MESSAGE_INDEX << "=" << ps.messageIndexStr << "\">Download</a></TD></TR>\n";
    ps.ostr1 << "</TABLE>\n";



    if (itsAnimationEnabled  &&  (ps.presentation == "Image" /*|| ps.presentation == "Map"*/ || ps.presentation == "Streams"))
    {
      ps.ostr2 << "<TABLE>\n";
      ps.ostr2 << "<TR><TD style=\"height:35; width:100%; vertical-align:middle; text-align:left; font-size:12;\">" << ps.ostr3.str() << "</TD></TR>\n";
    }
    else
    {
      ps.ostr2 << "<TABLE width=\"100%\" height=\"100%\">\n";
    }

    if (ps.presentation == "Image")
    {
      ps.ostr2 << R"(<TR><TD style="vertical-align:top;"><IMG id="myimage" style="background:#000000; max-width:1800; height:100%; max-height:1000;" src="/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << ps.presentation << "\" onclick=\"getImageCoords(event,this," << ps.fileIdStr << "," << ps.messageIndexStr << ",'" << ps.presentation << "','" << session.getUrlParameter() << "');\"/></TD></TR>";
    }
    else if (ps.presentation == "Map")
    {
      ps.ostr2 << R"(<TR><TD><IMG id="myimage" style="background:#000000; max-width:1800; height:100%; max-height:1000;" src="/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << ps.presentation << "\"/></TD></TR>";
    }
    else if (ps.presentation == "Table(sample)" /* || ps.presentation == "table(full)"*/)
    {
      ps.ostr2 << R"(<TR><TD><IFRAME width="100%" height="100%" src="grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PAGE << "=table\">";
      ps.ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ps.ostr2 << "</IFRAME></TD></TR>";
    }
    else if (ps.presentation == "Coordinates(sample)" /* || ps.presentation == "coordinates(full)"*/)
    {
      ps.ostr2 << R"(<TR><TD><IFRAME width="100%" height="100%" src="grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PAGE << "=coordinates\">";
      ps.ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ps.ostr2 << "</IFRAME></TD></TR>";
    }
    else if (ps.presentation == "Info")
    {
      ps.ostr2 << R"(<TR><TD><IFRAME width="100%" height="100%" src="grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << ps.presentation << "\">";
      ps.ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ps.ostr2 << "</IFRAME></TD></TR>";
    }
    else if (ps.presentation == "Streams" || ps.presentation == "StreamsAnimation")
    {
      ps.ostr2 << R"(<TR><TD><IMG id="myimage" style="background:#000000; max-width:1800; height:100%; max-height:1000;" src="/grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << ps.presentation << "\" onclick=\"getImageCoords(event,this," << ps.fileIdStr << "," << ps.messageIndexStr << ",'" << ps.presentation << "','" << session.getUrlParameter() << "');\"/></TD></TR>";
    }
    else if (ps.presentation == "Message")
    {
      ps.ostr2 << R"(<TR><TD><IFRAME width="100%" height="100%" src="grid-gui?session=)" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << ps.presentation << "\">";
      ps.ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ps.ostr2 << "</IFRAME></TD></TR>";
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




int Plugin::page_main(const Spine::Reactor & /*theReactor*/,
                            const HTTP::Request & /*theRequest*/,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    PageMainState ps;
    page_main_selectionAttributes(ps,session);
    page_main_producers(ps,session);
    page_main_generations(ps,session);
    page_main_parameters(ps,session);
    page_main_levelIds(ps,session);
    page_main_levels(ps,session);
    page_main_forecastTypes(ps,session);
    page_main_forecastNumbers(ps,session);
    page_main_geometries(ps,session);
    page_main_times(ps,session);
    page_main_presentation(ps,session);
    page_main_projections(ps,session);
    page_main_colorMaps(ps,session);
    page_main_paintOptions(ps,session);
    page_main_imageFrame(ps,session);

    if (ps.currentCont)
    {
      ps.aggregation = aggregationText(*ps.currentCont);
      ps.processing = processingText(*ps.currentCont);
    }

    ps.ostr2 << "<TR><TD style=\"height:25; vertical-align:middle; text-align:left; font-size:12;\">" << ps.paramDescription << ps.aggregation << ps.processing << "</TD></TR>\n";

    ps.ostr2 << "</TABLE>\n";


    if (ps.presentation == "Image" || ps.presentation == "Map" || ps.presentation == "Streams")
    {
      ps.output << "<TABLE height=\"100%\">\n";
    }
    else
    {
      ps.output << "<TABLE height=\"100%\" width=\"100%\">\n";
    }

    ps.output << "<TR>\n";

    ps.output << "<TD style=\"vertical-align:top; background:#C0C0C0; width:290;\">\n";
    ps.output << ps.ostr1.str();
    ps.output << "</TD>\n";

    ps.output << "<TD  style=\"vertical-align:top;\">\n";
    ps.output << ps.ostr2.str();
    ps.output << "</TD>\n";

    if (itsAnimationEnabled  &&  (ps.presentation == "Image" /*|| ps.presentation == "Map"*/ || ps.presentation == "Streams"))
    {
      ps.output << "<TD style=\"vertical-align:top; width:70;\">\n";
      ps.output << ps.ostr4.str();
      ps.output << "</TD>\n";
    }

    ps.output << "</TR>\n";
    ps.output << "</TABLE>\n";
    ps.output << "</BODY></HTML>\n";

    theResponse.setContent(std::string(ps.output.str()));
    theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
    return HTTP::Status::ok;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





namespace
{
/*! \brief Updates the session attributes from the request parameters. The previous
    value of a changed attribute is stored with the '#' prefix. */

void updateSessionAttributes(Session& session,const Spine::HTTP::ParamMap& map)
{
  for (const auto& it : map)
  {
    //printf("SEARCH [%s][%s]\n",it->first.c_str(),it->second.c_str());
    std::string value;
    if (strcasecmp(it.first.c_str(),"session") != 0  &&  session.getAttribute(it.first.c_str(),value))
    {
      if (value != it.second)
      {
        session.setAttribute(it.first.c_str(),it.second.c_str());
        std::string name = std::string("#") + it.first;
        session.setAttribute(name.c_str(),value.c_str());
      }
    }
  }
}
}  // namespace

/*! \brief Calls the handler of the requested page. Unknown pages produce an empty
    response. The cache expiration time of the page is set to expires_seconds. */

int Plugin::page_dispatch(const std::string& page,
                            const Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session,
                            int& expires_seconds)
{
  using PageHandler = int (Plugin::*)(const Spine::Reactor&,const HTTP::Request&,HTTP::Response&,Session&);

  struct PageInfo
  {
    const char *name;
    PageHandler handler;
    int expires_seconds;   // 0 = keep the default
  };

  static const PageInfo pages[] =
  {
    {"main",&Plugin::page_main,0},
    {"image",&Plugin::page_image,600},
    {"streams",&Plugin::page_streams,600},
    {"streamsAnimation",&Plugin::page_streamsAnimation,600},
    {"map",&Plugin::page_map,600},
    {"info",&Plugin::page_info,0},
    {"message",&Plugin::page_message,0},
    {"download",&Plugin::page_download,0},
    {"table",&Plugin::page_table,600},
    {"coordinates",&Plugin::page_coordinates,600},
    {"value",&Plugin::page_value,0}
  };

  for (const auto& info : pages)
  {
    if (strcasecmp(page.c_str(),info.name) == 0)
    {
      int result = (this->*info.handler)(theReactor,theRequest,theResponse,session);
      if (info.expires_seconds > 0)
        expires_seconds = info.expires_seconds;
      return result;
    }
  }
  return HTTP::Status::ok;
}





/*! \brief GridGui: Request. */

int Plugin::request(const Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  FUNCTION_TRACE
  try
  {
    int result = HTTP::Status::ok;
    int expires_seconds = 1;

    Session session;

    Spine::HTTP::ParamMap map = theRequest.getParameterMap();
    for (const auto& param : map)
    {
      if (!isSafeRequestValue(param.first) || !isSafeRequestValue(param.second))
      {
        theResponse.setHeader("Content-Type", "text/plain; charset=UTF-8");
        theResponse.setContent("Invalid characters in request parameter\n");
        return HTTP::Status::bad_request;
      }
    }

    // The new user interface
    const std::string resource = theRequest.getResource();
    if (!itsUiUrl.empty() && resource.compare(0,itsUiUrl.size(),itsUiUrl) == 0 &&
        (resource.size() == itsUiUrl.size() || resource[itsUiUrl.size()] == '/'))
    {
      if (!itsGridEngine->isEnabled())
      {
        theResponse.setHeader("Content-Type", "text/plain; charset=UTF-8");
        theResponse.setContent("The grid engine is disabled\n");
        return HTTP::Status::service_unavailable;
      }
      return gui_request(theReactor,theRequest,theResponse,resource.substr(itsUiUrl.size()));
    }

    std::optional<std::string> v;
    v = theRequest.getParameter("session");
    if (v)
    {
      //printf("**** SESSION FOUND\n");
      session.setAttributes(*v);
      //session.print(std::cout,0,0);
    }
    else
      initSession(session);

    updateSessionAttributes(session,map);


    if (!itsGridEngine->isEnabled())
    {
      std::ostringstream output;
      output << "<HTML><BODY>\n";
      output << "<B>Grid-gui cannot be used because the grid-engine is disabled!</B>\n";
      output << "</BODY></HTML>\n";

      theResponse.setContent(std::string(output.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
    }

    std::string page = "main";
    session.getAttribute(ATTR_PAGE,page);

    result = page_dispatch(page,theReactor,theRequest,theResponse,session,expires_seconds);

    Fmi::DateTime t_now = Fmi::SecondClock::universal_time();
    Fmi::DateTime t_expires = t_now + Fmi::Seconds(expires_seconds);
    std::shared_ptr<Fmi::TimeFormatter> tformat(Fmi::TimeFormatter::create("http"));
    std::string cachecontrol = "public, max-age=" + Fmi::to_string(expires_seconds);
    std::string expiration = tformat->format(t_expires);
    std::string modification = tformat->format(t_now);

    theResponse.setHeader("Cache-Control", cachecontrol);
    theResponse.setHeader("Expires", expiration);

    if (result == HTTP::Status::ok)
      theResponse.setHeader("Last-Modified", modification);

    return result;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





// ----------------------------------------------------------------------
/*!
 * \brief Main request handler
 */
// ----------------------------------------------------------------------

/*! \brief GridGui: Request handler. */

void Plugin::requestHandler(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  FUNCTION_TRACE
  try
  {
    try
    {
      //auto headers = theRequest.getHeaders();
      //for (auto it = headers.begin(); it != headers.end(); ++it)
      //  std::cout << it->first << " = " << it->second << "\n";

      // No CORS header: grid-gui is an internal tool and its pages and images must not
      // be readable by scripts on other origins.
      int status = request(theReactor, theRequest, theResponse);
      theResponse.setStatus(status);
    }
    catch (...)
    {
      // Catching all exceptions

      Fmi::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", theRequest.getURI());
      exception.printError();

      theResponse.setStatus(HTTP::Status::bad_request);

      // Adding the first exception information into the response header

      std::string firstMessage = exception.what();
      boost::algorithm::replace_all(firstMessage, "\n", " ");
      firstMessage = firstMessage.substr(0, 300);
      theResponse.setHeader("X-Content-Error", firstMessage);

    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




// ----------------------------------------------------------------------
/*!
 * \brief Return the plugin name
 */
// ----------------------------------------------------------------------

const std::string &Plugin::getPluginName() const
{
  return itsModuleName;
}





// ----------------------------------------------------------------------
/*!
 * \brief Return the required version
 */
// ----------------------------------------------------------------------

/*! \brief GridGui: Get required api version. */

int Plugin::getRequiredAPIVersion() const
{
  return SMARTMET_API_VERSION;
}




// ----------------------------------------------------------------------
/*!
 * \brief This is an admin plugin
 */
// ----------------------------------------------------------------------

/*! \brief GridGui: Is admin query. */

bool Plugin::isAdminQuery(const Spine::HTTP::Request & /* theRequest */) const
{
  return true;
}

}  // namespace GridContent
}  // namespace Plugin
}  // namespace SmartMet


/*
 * Server knows us through the 'SmartMetPlugin' virtual interface, which
 * the 'Plugin' class implements.
 */

extern "C" SmartMetPlugin *create(SmartMet::Spine::Reactor *them, const char *config)
{
  return new SmartMet::Plugin::GridGui::Plugin(them, config);
}


extern "C" void destroy(SmartMetPlugin *us)
{
  // This will call 'Plugin::~Plugin()' since the destructor is virtual
  delete us;
}

// ======================================================================
