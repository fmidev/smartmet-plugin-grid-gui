// ======================================================================
/*!
 * \brief SmartMet Grid Gui plugin implementation
 */
// ======================================================================

#include "Plugin.h"

#include <grid-files/common/GeneralFunctions.h>
#include <grid-files/common/ImagePaint.h>
#include <grid-files/identification/GridDef.h>
#include <grid-files/common/ShowFunction.h>
#include <spine/SmartMet.h>
#include <macgyver/TimeFormatter.h>
#include <macgyver/FastMath.h>
#include <boost/bind/bind.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <webp/encode.h>
#include <webp/mux.h>

#define FUNCTION_TRACE FUNCTION_TRACE_OFF


namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{

using namespace SmartMet::Spine;

/*
#define ATTR_BACKGROUND         "background"
#define ATTR_BLUR               "blur"
#define ATTR_COLOR_MAP          "colorMap"
#define ATTR_COORDINATE_LINES   "coordinateLines"
#define ATTR_DALI_ID            "daliId"
#define ATTR_FILE_ID            "fileId"
#define ATTR_FMI_KEY            "fmiKey"
#define ATTR_FORECAST_NUMBER    "forecastNumber"
#define ATTR_FORECAST_TYPE      "forecastType"
#define ATTR_GENERATION_ID      "generationId"
#define ATTR_GEOMETRY_ID        "geometryId"
#define ATTR_HUE                "hue"
#define ATTR_ISOLINES           "isolines"
#define ATTR_ISOLINE_VALUES     "isolineValues"
#define ATTR_LAND_BORDER        "landBorder"
#define ATTR_LAND_MASK          "landMask"
#define ATTR_LEVEL              "level"
#define ATTR_LEVEL_ID           "levelId"
#define ATTR_LOCATIONS          "locations"
#define ATTR_MAX_LENGTH         "maxLength"
#define ATTR_MESSAGE_INDEX      "messageIndex"
#define ATTR_MIN_LENGTH         "minLength"
#define ATTR_MISSING            "missing"
#define ATTR_PAGE               "page"
#define ATTR_PARAMETER_ID       "parameterId"
#define ATTR_PRESENTATION       "presentation"
#define ATTR_PRODUCER_ID        "producerId"
#define ATTR_PRODUCER_NAME      "producerName"
#define ATTR_PROJECTION_ID      "projectionId"
#define ATTR_SATURATION         "saturation"
#define ATTR_SEA_MASK           "seaMask"
#define ATTR_STEP               "step"
#define ATTR_SYMBOL_MAP         "symbolMap"
#define ATTR_TIME               "time"
#define ATTR_UNIT               "unit"
#define ATTR_X                  "x"
#define ATTR_Y                  "y"
*/

#define ATTR_BACKGROUND         "a"
#define ATTR_BLUR               "b"
#define ATTR_COLOR_MAP          "c"
#define ATTR_COORDINATE_LINES   "d"
//#define ATTR_DALI_ID            "e"
#define ATTR_FILE_ID            "f"
#define ATTR_FMI_KEY            "g"
#define ATTR_FORECAST_NUMBER    "h"
#define ATTR_FORECAST_TYPE      "i"
#define ATTR_GENERATION_ID      "j"
#define ATTR_GEOMETRY_ID        "k"
#define ATTR_HUE                "l"
#define ATTR_ISOLINES           "m"
#define ATTR_ISOLINE_VALUES     "n"
#define ATTR_LAND_BORDER        "o"
#define ATTR_LAND_MASK          "p"
#define ATTR_LEVEL              "q"
#define ATTR_LEVEL_ID           "r"
#define ATTR_LOCATIONS          "s"
#define ATTR_MAX_LENGTH         "t"
#define ATTR_MESSAGE_INDEX      "u"
#define ATTR_MIN_LENGTH         "v"
#define ATTR_MISSING            "w"
#define ATTR_PAGE               "x"
#define ATTR_PARAMETER_ID       "y"
#define ATTR_PRESENTATION       "z"
#define ATTR_PRODUCER_ID        "a0"
#define ATTR_PRODUCER_NAME      "a1"
#define ATTR_PROJECTION_ID      "a2"
#define ATTR_SATURATION         "a3"
#define ATTR_SEA_MASK           "a4"
#define ATTR_STEP               "a5"
#define ATTR_SYMBOL_MAP         "a6"
#define ATTR_TIME               "a7"
#define ATTR_UNIT               "a8"
#define ATTR_X                  "xx"
#define ATTR_Y                  "yy"


// ----------------------------------------------------------------------
/*!
 * \brief Plugin constructor
 */
// ----------------------------------------------------------------------

Plugin::Plugin(Spine::Reactor *theReactor, const char *theConfig)
    : SmartMetPlugin(), itsModuleName("GridGui")
{
  using namespace boost::placeholders;
  try
  {
    const char *configAttribute[] =
    {
        "smartmet.plugin.grid-gui.grid-files.configFile",
        "smartmet.plugin.grid-gui.land-sea-mask-file",
        "smartmet.plugin.grid-gui.colorMapFiles",
        "smartmet.plugin.grid-gui.symbolMapFiles",
        "smartmet.plugin.grid-gui.locationFiles",
        "smartmet.plugin.grid-gui.colorFile",
        //"smartmet.plugin.grid-gui.daliFile",
        "smartmet.plugin.grid-gui.isolineFile",
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

    if (theReactor->getRequiredAPIVersion() != SMARTMET_API_VERSION)
      throw Fmi::Exception(BCP, "GridGui plugin and Server API version mismatch");

    // Register the handler
    if (!theReactor->addPrivateContentHandler(
            this, "/grid-gui", boost::bind(&Plugin::callRequestHandler, this, _1, _2, _3)))
      throw Fmi::Exception(BCP, "Failed to register GridGui request handler");


    itsConfigurationFile.readFile(theConfig);

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
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.land-sea-mask-file",itsLandSeaMaskFile);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.colorMapFiles",itsColorMapFileNames);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.symbolMapFiles",itsSymbolMapFileNames);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.locationFiles",itsLocationFileNames);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.colorFile",itsColorFile);
    //itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.daliFile",itsDaliFile);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.isolineFile",itsIsolineFile);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.animationEnabled",itsAnimationEnabled);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.imageCache.directory",itsImageCache_dir);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.imageCache.maxImages",itsImageCache_maxImages);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.imageCache.minImages",itsImageCache_minImages);


    Identification::gridDef.init(itsGridConfigFile.c_str());

    jpg_load(itsLandSeaMaskFile.c_str(),itsLandSeaMask);


    for (auto it = itsColorMapFileNames.begin(); it != itsColorMapFileNames.end(); ++it)
    {
      T::ColorMapFile colorMapFile;
      colorMapFile.init(it->c_str());
      itsColorMapFiles.emplace_back(colorMapFile);
    }

    for (auto it = itsSymbolMapFileNames.begin(); it != itsSymbolMapFileNames.end(); ++it)
    {
      T::SymbolMapFile symbolMapFile;
      symbolMapFile.init(it->c_str());
      itsSymbolMapFiles.emplace_back(symbolMapFile);
    }

    for (auto it = itsLocationFileNames.begin(); it != itsLocationFileNames.end(); ++it)
    {
      T::LocationFile locationFile;
      locationFile.init(it->c_str());
      itsLocationFiles.emplace_back(locationFile);
    }

    loadColorFile();
    loadIsolineFile();

    /*
    if (itsDaliFile > " ")
      loadDaliFile();
    */

    // Removing files from the image cache.

    std::vector<std::string> filePatterns;
    std::set<std::string> dirList;
    std::vector<std::pair<std::string,std::string>> fileList;

    filePatterns.emplace_back(std::string("grid-gui-image_*"));

    getFileList(itsImageCache_dir.c_str(),filePatterns,false,dirList,fileList);

    for (auto it = fileList.begin(); it != fileList.end(); ++it)
    {
      std::string fname = itsImageCache_dir + "/" + it->second;
      remove(fname.c_str());
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

Plugin::~Plugin()
{
}


// ----------------------------------------------------------------------
/*!
 * \brief Initializator, in this case trivial
 */
// ----------------------------------------------------------------------
void Plugin::init()
{
  FUNCTION_TRACE
  try
  {
    auto engine = itsReactor->getSingleton("grid", nullptr);
    if (!engine)
      throw Fmi::Exception(BCP, "The 'grid-engine' unavailable!");

    itsGridEngine = reinterpret_cast<Engine::Grid::Engine*>(engine);


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





T::ColorMapFile* Plugin::getColorMapFile(std::string colorMapName)
{
  FUNCTION_TRACE
  try
  {
    T::ColorMapFile *map = nullptr;
    for (auto it = itsColorMapFiles.begin(); it != itsColorMapFiles.end(); ++it)
    {
      it->checkUpdates();
      if (it->hasName(colorMapName.c_str()))
        map = &(*it);
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

    while (!feof(file))
    {
      if (fgets(st,1000,file) != nullptr  &&  st[0] != '#')
      {
        bool ind = false;
        char *field[100];
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

        if (c > 1)
        {
          if (field[0][0] != '\0'  &&  field[1][0] != '\0')
          {
            std::string colorName = field[0];
            uint color = strtoul(field[1],nullptr,16);

            itsColors.emplace_back(std::pair<std::string,unsigned int>(colorName,color));
          }
        }
      }
    }
    fclose(file);

    itsColors_lastModified = getFileModificationTime(itsColorFile.c_str());
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




/*
void Plugin::loadDaliFile()
{
  FUNCTION_TRACE
  try
  {
    FILE *file = fopen(itsDaliFile.c_str(),"re");
    if (file == nullptr)
    {
      Fmi::Exception exception(BCP,"Cannot open file!");
      exception.addParameter("Filename",itsDaliFile);
      throw exception;
    }

    itsDaliProducts.clear();

    char st[1000];

    while (!feof(file))
    {
      if (fgets(st,1000,file) != nullptr  &&  st[0] != '#')
      {
        bool ind = false;
        char *field[100];
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

        if (c > 5)
        {
          string_vec plist;

          splitString(field[0],',',plist);
          for (auto it=plist.begin(); it!=plist.end();++it)
          {
            string_vec vec;
            vec.emplace_back(*it);
            vec.emplace_back(field[1]);
            vec.emplace_back(field[2]);
            vec.emplace_back(field[3]);
            vec.emplace_back(field[4]);
            vec.emplace_back(field[5]);

            itsDaliProducts.emplace_back(vec);
          }
        }
      }
    }
    fclose(file);

    itsDaliFile_lastModified = getFileModificationTime(itsDaliFile.c_str());
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}
*/




void Plugin::loadIsolineFile()
{
  FUNCTION_TRACE
  try
  {
    if (itsIsolineFile.empty())
      return;

    FILE *file = fopen(itsIsolineFile.c_str(),"re");
    if (file == nullptr)
    {
      Fmi::Exception exception(BCP,"Cannot open file!");
      exception.addParameter("Filename",itsIsolineFile);
      throw exception;
    }

    itsIsolines.clear();

    char st[1000];

    while (!feof(file))
    {
      if (fgets(st,1000,file) != nullptr  &&  st[0] != '#')
      {
        bool ind = false;
        char *field[100];
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

        if (c > 1)
        {
          if (field[0][0] != '\0'  &&  field[1][0] != '\0')
          {
            std::string isolineName = field[0];
            T::ParamValue_vec values;

            for (uint t=1; t<c; t++)
            {
              float val = toFloat(field[t]);
              values.emplace_back(val);
            }

            itsIsolines.insert(std::pair<std::string,T::ParamValue_vec>(isolineName,values));
          }
        }
      }
    }
    fclose(file);

    itsColors_lastModified = getFileModificationTime(itsColorFile.c_str());
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





uint Plugin::getColorValue(std::string& colorName)
{
  FUNCTION_TRACE
  try
  {
    for (auto it = itsColors.begin(); it != itsColors.end(); ++it)
    {
      if (it->first == colorName)
        return it->second;
    }

    return 0xFFFFFFFF;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
 }
}





T::ParamValue_vec Plugin::getIsolineValues(std::string& isolineValues)
{
  FUNCTION_TRACE
  try
  {
    auto it = itsIsolines.find(isolineValues);
    if (it != itsIsolines.end())
      return it->second;

    T::ParamValue_vec emptyVector;
    return emptyVector;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





T::SymbolMapFile* Plugin::getSymbolMapFile(std::string symbolMap)
{
  FUNCTION_TRACE
  try
  {
    T::SymbolMapFile *map = nullptr;
    for (auto it = itsSymbolMapFiles.begin(); it != itsSymbolMapFiles.end(); ++it)
    {
      it->checkUpdates();
      if (it->hasName(symbolMap.c_str()))
        map = &(*it);
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





T::LocationFile* Plugin::getLocationFile(std::string name)
{
  FUNCTION_TRACE
  try
  {
    T::LocationFile *file = nullptr;
    for (auto it = itsLocationFiles.begin(); it != itsLocationFiles.end(); ++it)
    {
      it->checkUpdates();
      if (it->hasName(name.c_str()))
        file = &(*it);
    }
    return file;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





bool Plugin::isLand(double lon,double lat)
{
  //FUNCTION_TRACE
  try
  {
    if (itsLandSeaMask.pixel == nullptr)
      return false;

    if (lon >= 180)
      lon = lon - 360;

    if (lat >= 90)
      lat = lat - 90;

    int x = C_INT(round((lon+180)*10));
    int y = C_INT(round((lat+90)*10));

    if (x >= 0  &&  x < itsLandSeaMask.width  &&  y >= 0  &&  y < itsLandSeaMask.height)
    {
      int pos = ((itsLandSeaMask.height-y-1)*itsLandSeaMask.width + x);

      if (pos >= 0  &&  pos < (itsLandSeaMask.height*itsLandSeaMask.width)  &&  itsLandSeaMask.pixel[pos] < 0x808080)
        return true;
    }
    return false;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





void Plugin::saveMap(const char *imageFile,uint columns,uint rows,T::ParamValue_vec&  values,unsigned char hue,unsigned char saturation,unsigned char blur,uint coordinateLines,uint landBorder,std::string landMask,std::string seaMask,std::string colorMapName,std::string missingStr)
{
  FUNCTION_TRACE
  try
  {
    bool zeroIsMissingValue = false;
    if (!missingStr.empty() &&  strcasecmp(missingStr.c_str(),"Zero") == 0)
      zeroIsMissingValue = true;


    uint landColor = getColorValue(landMask);
    uint seaColor = getColorValue(seaMask);

    if (landColor == 0xFFFFFFFF)
      landColor = 0xFFFFFF;

    if (seaColor == 0xFFFFFFFF)
      seaColor = 0xFFFFFF;

    T::ColorMapFile *colorMapFile = nullptr;

    if (!colorMapName.empty() &&  strcasecmp(colorMapName.c_str(),"None") != 0)
      colorMapFile = getColorMapFile(colorMapName);


    uint sz = values.size();

    if (sz == 0)
      return;

    if (sz != (columns * rows))
    {
      printf("The number of values (%u) does not match to the grid size (%u x %u)1\n",sz,columns,rows);
      exit(-1);
    }

    double maxValue = -1000000000;
    double minValue = 1000000000;
    double total = 0;
    uint cnt = 0;

    for (uint t=0; t<sz; t++)
    {
      double val = values[t];
      if (val != ParamValueMissing)
      {
        total = total + val;
        cnt++;
        if (val < minValue)
          minValue = val;

        if (val > maxValue)
          maxValue = val;
      }
    }

    int width = columns;
    int height = rows;

    uint xx = columns / 36;
    uint yy = rows / 18;

    double dWidth = C_DOUBLE(width);
    double dHeight = C_DOUBLE(height);

    double xd = 360/dWidth;
    double yd = 180/dHeight;

    double avg = total / (double)cnt;
    double dd = maxValue - minValue;
    double ddd = avg-minValue;
    double step = dd / 200;
    if (maxValue > (minValue + 5*ddd))
      step = 5*ddd / 200;

    uint *image = new uint[width*height];

    uint c = 0;
    bool yLand[width];
    for (int x=0; x<width; x++)
      yLand[x] = false;

    for (int y=0; y<height; y++)
    {
      bool prevLand = false;
      for (int x=0; x<width; x++)
      {
        T::ParamValue val = values[c];
        if (val == 0.0   &&  zeroIsMissingValue)
          val = ParamValueMissing;

        uint vv = ((val - minValue) / step);
        uint v = 200 - vv;
        if (vv > 200)
          v = 0;

        v = v / blur;
        v = v * blur;
        v = v + 55;
        uint col = hsv_to_rgb(hue,saturation,C_UCHAR(v));

        if (colorMapFile != nullptr)
          col = colorMapFile->getColor(val);

        double xc = xd*(x-(dWidth/2));
        double yc = yd*((dHeight-y-1)-(dHeight/2));

        bool land = isLand(xc,yc);

        if (land  &&  (val == ParamValueMissing || (col & 0xFF000000)))
          col = landColor;

        if (!land &&  (val == ParamValueMissing || (col & 0xFF000000)))
          col = seaColor;

        if (landBorder != 0xFFFFFFFF)
        {
          if (land & (!prevLand || !yLand[x]))
            col = landBorder;

          if (!land)
          {
            if (prevLand  &&  x > 0  &&  image[y*width + x-1] != coordinateLines)
              image[y*width + x-1] = landBorder;

            if (yLand[x] &&  y > 0  && image[(y-1)*width + x] != coordinateLines)
              image[(y-1)*width + x] = landBorder;
          }
        }

        if (coordinateLines != 0xFFFFFFFF && ((x % xx) == 0  ||  (y % yy) == 0))
        {
          col = coordinateLines;
        }

        yLand[x] = land;
        prevLand = land;
        image[y*width + x] = col;
        c++;
      }
    }

    jpeg_save(imageFile,image,height,width,100);
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

      for (auto it = itsImages.begin(); it != itsImages.end(); ++it)
      {
        tmpImages.insert(std::pair<std::string,std::string>(it->second,it->first));
      }

      for (auto it = tmpImages.begin(); it != tmpImages.end()  &&  cnt > itsImageCache_minImages; ++it)
      {
        remove(it->first.c_str());
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





void Plugin::saveImage(
    const char *imageFile,
    uint fileId,
    uint messageIndex,
    unsigned char hue,
    unsigned char saturation,
    unsigned char blur,
    uint coordinateLines,
    uint isolines,
    std::string isolineValues,
    uint landBorder,
    std::string landMask,
    std::string seaMask,
    std::string colorMapName,
    std::string missingStr,
    T::GeometryId geometryId,
    T::GeometryId projectionId,
    std::string symbolMap,
    std::string locations,
    bool showSymbols,
    int pstep,
    int minLength,
    int maxLength,
    bool lightBackground,
    bool animation)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    T::GeometryId geomId = geometryId;
    if (projectionId > 0  &&  projectionId != geometryId)
      geomId = projectionId;

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


/*
    if (coordinates.size() == 0  &&  gridData.mGeometryId != 0)
    {
      coordinates = Identification::gridDef.getGridLatLonCoordinatesByGeometryId(gridData.mGeometryId);
      geometryId = gridData.mGeometryId;
    }
*/
/*
    T::GridCoordinates coordinatesx;
    int result = dataServer->getGridCoordinates(0,fileId,messageIndex,T::CoordinateTypeValue::LATLON_COORDINATES,coordinatesx);
    coordinates = coordinatesx.mCoordinateList;
*/

    T::Coordinate_svec lineCoordinatesPtr;
    if (coordinateLines != 0xFFFFFFFF)
    {
      if (geomId != 0)
      {
        lineCoordinatesPtr = Identification::gridDef.getGridLatLonCoordinateLinePointsByGeometryId(geomId);
        if (lineCoordinatesPtr)
          lineCoordinates = lineCoordinatesPtr.get();
      }
/*
      if (lineCoordinates.size() == 0  &&  gridData.mGeometryId != 0)
        lineCoordinates = Identification::gridDef.getGridLatLonCoordinateLinePointsByGeometryId(gridData.mGeometryId);
*/
    }

    if (geomId == geometryId)
    {
      T::GridData gridData;
      int result = dataServer->getGridData(0,fileId,messageIndex,gridData);
      if (result != 0)
      {
        Fmi::Exception exception(BCP,"Data fetching failed!");
        exception.addParameter("Result",DataServer::getResultString(result));
        throw exception;
      }

      saveImage(imageFile,gridData.mColumns,gridData.mRows,gridData.mValues,*coordinates,*lineCoordinates,hue,saturation,blur,coordinateLines,isolines,isolineValues,landBorder,landMask,seaMask,colorMapName,missingStr,geometryId,symbolMap,locations,showSymbols,pstep,minLength,maxLength,lightBackground,animation);
    }
    else
    {
      uint cols = 0;
      uint rows = 0;
      if (Identification::gridDef.getGridDimensionsByGeometryId(geomId,cols,rows))
      {
        T::ParamValue_vec values;

        short interpolationMethod = T::AreaInterpolationMethod::Linear;
        if (showSymbols)
          interpolationMethod = T::AreaInterpolationMethod::Nearest;

        T::AttributeList attributeList;
        attributeList.addAttribute("grid.geometryId",std::to_string(geomId));
        attributeList.addAttribute("grid.areaInterpolationMethod",std::to_string(interpolationMethod));
        if (fileId > 0)
        {
          double_vec modificationParameters;
          int result = dataServer->getGridValueVectorByGeometry(0,fileId,messageIndex,attributeList,0,modificationParameters,values);
          if (result != 0)
            throw Fmi::Exception(BCP,"Data fetching failed!");

          saveImage(imageFile,cols,rows,values,*coordinates,*lineCoordinates,hue,saturation,blur,coordinateLines,isolines,isolineValues,landBorder,landMask,seaMask,colorMapName,missingStr,geomId,symbolMap,locations,showSymbols,pstep,minLength,maxLength,lightBackground,animation);
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





void Plugin::saveImage(
    const char *imageFile,
    int width,
    int height,
    T::ParamValue_vec& values,
    T::Coordinate_vec& coordinates,
    T::Coordinate_vec& lineCoordinates,
    unsigned char hue,
    unsigned char saturation,
    unsigned char blur,
    uint coordinateLines,
    uint isolines,
    std::string isolineValues,
    uint landBorder,
    std::string landMask,
    std::string seaMask,
    std::string colorMapName,
    std::string missingStr,
    T::GeometryId geometryId,
    std::string symbolMap,
    std::string locations,
    bool showSymbols,
    int pstep,
    int minLength,
    int maxLength,
    bool lightBackground,
    bool animation)
{
  FUNCTION_TRACE
  try
  {
    T::ColorMapFile *colorMapFile = nullptr;

    if (!colorMapName.empty() &&  strcasecmp(colorMapName.c_str(),"None") != 0)
      colorMapFile = getColorMapFile(colorMapName);

    bool zeroIsMissingValue = false;
    if (!missingStr.empty() &&  strcasecmp(missingStr.c_str(),"Zero") == 0)
      zeroIsMissingValue = true;

    uint landColor = getColorValue(landMask);
    uint seaColor = getColorValue(seaMask);

    if (landColor == 0xFFFFFFFF)
      landColor = 0xFFFFFF;

    if (seaColor == 0xFFFFFFFF)
      seaColor = 0xFFFFFF;

    bool showIsolines = true;
    if ((isolines & 0xFF000000) != 0)
      showIsolines = false;

    bool showValues = true;
    if (showSymbols || showIsolines)
      showValues = false;

    uint size = width*height;
    std::size_t sz = values.size();

    if (size == 0)
      return;

    if (sz < size)
    {
      printf("ERROR: There are not enough values (= %lu) for the grid (%u x %u)!\n",sz,width,height);
      return;
    }

    double maxValue = -1000000000;
    double minValue = 1000000000;
    double total = 0;
    uint cnt = 0;

    for (uint t=0; t<size; t++)
    {
      double val = values[t];
      if (val != ParamValueMissing)
      {
        total = total + val;
        cnt++;
        if (val < minValue)
          minValue = val;

        if (val > maxValue)
          maxValue = val;
      }
    }

    bool rotate = true;
    if (coordinates.size() > C_UINT(10*width)  &&  coordinates[0].y() < coordinates[10*width].y())
      rotate = true;
    else
      rotate = false;


    ImagePaint imagePaint(width,height,0x0,isolines,0xFFFFFFFF,false,rotate);

    bool landSeaMask = true;
    if (coordinates.size() == 0)
      landSeaMask = false;

    double avg = total / (double)cnt;
    double dd = maxValue - minValue;
    double ddd = avg-minValue;
    double step = dd / 200;
    if (maxValue > (minValue + 5*ddd))
      step = 5*ddd / 200;

    if (blur == 0)
      blur = 1;

    T::ByteData_vec contours;

    if (showIsolines)
    {
      T::ParamValue_vec contourValues = getIsolineValues(isolineValues);

      if (contourValues.size() == 0)
      {
        double stp = dd / 10;
        for (int t=0; t<10; t++)
          contourValues.emplace_back(minValue + t*stp);
      }
      getIsolines(values,nullptr,width,height,contourValues,T::AreaInterpolationMethod::Linear,3,3,contours);
    }

    uint c = 0;

    bool yLand[width];
    for (int x=0; x<width; x++)
      yLand[x] = false;

    for (int y=0; y<height; y++)
    {
      bool prevLand = false;
      for (int x=0; x<width; x++)
      {
        T::ParamValue val = values[c];

        if (val == 0.0   &&  zeroIsMissingValue)
          val = ParamValueMissing;

        uint vv = ((val - minValue) / step);
        uint v = 200 - vv;
        if (vv > 200)
          v = 0;

        v = v / blur;
        v = v * blur;
        v = v + 55;
        uint col = hsv_to_rgb(hue,saturation,C_UCHAR(v));

        if (pstep > 0)
          col = 0xFFFFFFFF;

        if (colorMapFile != nullptr)
          col = colorMapFile->getColor(val);

        bool land = false;
        if (landSeaMask && c < coordinates.size())
          land = isLand(coordinates[c].x(),coordinates[c].y());

        if (land &&  (val == ParamValueMissing || (col & 0xFF000000) || !showValues))
          col = landColor;

        if (!land &&  (val == ParamValueMissing || (col & 0xFF000000) || !showValues))
          col = seaColor;

        if (landBorder != 0xFFFFFFFF)
        {
          if (land & (!prevLand || !yLand[x]))
            col = landBorder;

          if (!land)
          {
            if (prevLand  &&  x > 0)
              imagePaint.paintPixel(x-1,y,landBorder);

            if (yLand[x] &&  y > 0)
              imagePaint.paintPixel(x,y-1,landBorder);
          }
        }

        yLand[x] = land;
        prevLand = land;
        if (col != 0xFFFFFFFF)
          imagePaint.paintPixel(x,y,col);
        c++;
      }
    }

    if (showIsolines)
      imagePaint.paintWkb(1,1,0,0,contours);

    if (coordinateLines != 0xFFFFFFFF  &&  lineCoordinates.size() > 0)
    {
      for (auto it = lineCoordinates.begin(); it != lineCoordinates.end(); ++it)
        imagePaint.paintPixel(C_INT(Fmi::floor(it->x())),C_INT(Fmi::floor(it->y())),coordinateLines);
    }

    if (showSymbols)
    {
      T::LocationFile *locationFile = getLocationFile(locations);
      T::SymbolMapFile* symbolMapFile = getSymbolMapFile(symbolMap);

      if (locationFile != nullptr  &&  symbolMapFile != nullptr)
      {
        T::Coordinate_vec cv = locationFile->getCoordinates();

        for (auto cd = cv.begin(); cd != cv.end(); ++cd)
        {
          double grid_i = 0;
          double grid_j = 0;

          if (Identification::gridDef.getGridPointByGeometryIdAndLatLonCoordinates(geometryId,cd->y(),cd->x(),grid_i,grid_j))
          {
            T::ParamValue val = values[C_INT(round(grid_j))*width + C_INT(round(grid_i))];

            CImage img;
            if (symbolMapFile->getSymbol(val,img))
            {
              int xx = C_INT(round(grid_i)) - img.width/2;
              int yy = C_INT(round(grid_j));
              int cc = 0;
              for (int y=0; y<img.height; y++)
              {
                for (int x=0; x<img.width; x++)
                {
                  uint col = img.pixel[cc];
                  if ((col & 0xFF000000) == 0)
                    imagePaint.paintPixel(xx+x,yy+img.height/2-y,col);

                  cc++;
                }
              }
            }
          }
        }
      }
    }

    if (pstep > 0)
    {
      // Draw stream lines

      T::ParamValue *direction = new float[size];
      if (!rotate)
      {
        uint idx = 0;
        for (int y=0; y<height; y++)
        {
          for (int x=0; x<width; x++)
          {
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
            direction[idx] = values[idx2];
            idx++;
            idx2++;
          }
        }
      }

      uint *img = imagePaint.getImage();
      uint *image = new uint[size];

      if (animation)
      {
        uint *wimage[16];
        for (uint t=0; t<16; t++)
          wimage[t] = new uint[size];

        getStreamlineImage(direction,nullptr,image,width,height,pstep,pstep,minLength,maxLength);

        uint color[16];
        for (uint t=0; t<16; t++)
        {
          uint cc = t * 0x10 + 0x0F;
          if (lightBackground)
            cc = (16-t) * 0x10 + 0x0F;

          color[t] = (cc << 16) + (cc << 8) + cc;
        }

        uint idx = 0;
        for (int y = 0; y < height; y++)
        {
          for (int x=0; x < width; x++)
          {
            uint col = image[idx];
            for (uint t=0; t<16; t++)
            {
              uint newCol = img[idx];
              if (col != 0)
              {
                newCol = color[15-((col+t) % 16)];
              }
              wimage[t][idx] = newCol;
            }
            idx++;
          }
        }

        webp_anim_save(imageFile,wimage,width,height,16,50);

        for (uint t=0; t<16; t++)
          delete [] wimage[t];

        delete [] direction;
        delete [] image;
        return;
      }
      else
      {
        if (lightBackground)
          getStreamlineImage(direction,nullptr,image,width,height,pstep,pstep,minLength,maxLength,0,1000000,0,255,0xFFFFFFFF);
        else
          getStreamlineImage(direction,nullptr,image,width,height,pstep,pstep,minLength,maxLength,0,1000000,255,0,0xFFFFFFFF);

        for (uint t=0; t<size; t++)
        {
          if (image[t] != 0xFFFFFFFF)
            img[t] = image[t];
        }
      }
      delete [] direction;
      delete [] image;
    }

    imagePaint.saveJpgImage(imageFile);

    checkImageCache();
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





void Plugin::saveTimeSeries(const char *imageFile,std::vector<T::ParamValue>& valueList,int idx,std::set<int> dayIdx)
{
  FUNCTION_TRACE
  try
  {
    T::ParamValue maxValue = -1000000000;
    T::ParamValue minValue = 1000000000;

    int len = valueList.size();


    int width = len * 3;
    int height = 100;

    uint size = width*height;

    if (size == 0)
      return;

    for (auto it = valueList.begin(); it != valueList.end(); ++it)
    {
      if (*it != ParamValueMissing)
      {
        if (*it < minValue)
          minValue = *it;

        if (*it > maxValue)
          maxValue = *it;
      }
    }

    double diff = maxValue - minValue;
    double dd = (height-2) / diff;
    double step = diff / 200;


    uint *image = new uint[size];
    memset(image,0xFF,size*sizeof(uint));

    for (int x=0; x<len; x++)
    {
      int xp = x*3;
      int yp = C_INT(dd*(valueList[x]-minValue)) + 1;
      uint v = 200 - C_UINT((valueList[x]-minValue) / step);
      v = v + 55;
      uint col = hsv_to_rgb(0,0,C_UCHAR(v));

      if (x == idx)
        col = 0xFF0000;

      for (uint w=0; w<3; w++)
      {
        for (int y=0; y<yp; y++)
        {
          int pos = ((height-y-1)*width + xp+w);
          if (pos >=0  && pos < C_INT(size))
            image[pos] = col;
        }
      }

      if (dayIdx.find(x) != dayIdx.end())
      {
        for (int y=0; y<height; y++)
        {
          int pos = (y*width + xp);
          if (pos >=0  && pos < C_INT(size))
            image[pos] = 0xA0A0A0;
        }
      }

    }
    jpeg_save(imageFile,image,height,width,100);
    delete[] image;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





int Plugin::page_info(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
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
    int result = contentServer->getContentInfo(0,toUInt32(fileIdStr),toUInt32(messageIndexStr),contentInfo);
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

    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Id</TD><TD>" << producerInfo.mProducerId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Name</TD><TD>" << producerInfo.mName << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Title</TD><TD>" << producerInfo.mTitle << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Description</TD><TD>" << producerInfo.mDescription << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">Generation</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Id</TD><TD>" << generationInfo.mGenerationId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Name</TD><TD>" << generationInfo.mName << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Description</TD><TD>" << generationInfo.mDescription << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Analysis time</TD><TD>" << generationInfo.mAnalysisTime << "</TD></TR>\n";
    if (generationInfo.mDeletionTime > 0)
      ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Deletion time</TD><TD>" << utcTimeFromTimeT(generationInfo.mDeletionTime) << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">File</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Id</TD><TD>" << fileInfo.mFileId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Server</TD><TD>" << fileInfo.mServer << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Protocol</TD><TD>" << C_INT(fileInfo.mProtocol) << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Type</TD><TD>" << C_INT(fileInfo.mFileType) << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Name</TD><TD>" << fileInfo.mName << "</TD></TR>\n";
    if (fileInfo.mDeletionTime > 0)
      ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Deletion time</TD><TD>" << utcTimeFromTimeT(fileInfo.mDeletionTime) << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">Parameter</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Message index</TD><TD>" << contentInfo.mMessageIndex << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">File Position</TD><TD>" << contentInfo.mFilePosition << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Size</TD><TD>" << contentInfo.mMessageSize << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Forecast time</TD><TD>" << contentInfo.getForecastTime() << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Level</TD><TD>" << contentInfo.mParameterLevel << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">FMI identifier</TD><TD>" << contentInfo.mFmiParameterId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">FMI name</TD><TD>" << contentInfo.getFmiParameterName() << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">FMI level identifier</TD><TD>" << toString(contentInfo.mFmiParameterLevelId) << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Forecast type</TD><TD>" << contentInfo.mForecastType << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Forecast number</TD><TD>" << contentInfo.mForecastNumber << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Geometry identifier</TD><TD>" << contentInfo.mGeometryId << "</TD></TR>\n";

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





int Plugin::page_message(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
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
    int result = dataServer->getGridMessageBytes(0,toUInt32(fileIdStr),toUInt32(messageIndexStr),messageBytes,messageSections);
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
    uint ssize = messageSections.size();

    uint rows = size / 16;
    if ((size % 16) != 0)
      rows++;

    if (rows > 1000)
      rows = 1000;

    char tmp[2000];
    char tmp2[2000];
    char *p = tmp;

    ostr << "<HTML><BODY>\n";
    ostr << "<TABLE border=\"1\" style=\"font-family:Arial; font-size:14; color:#000000; background:#FFFFFF;\">\n";
    ostr << "<TR bgColor=\"#A0A0A0\"><TD width=\"50\">Address</TD>";


    for (uint c=0; c<16; c++)
      p += sprintf(p,"<TD width=\"20\" align=\"center\">%02X</TD>",c);

    p += sprintf(p,"<TD width=\"20\"></TD>");

    for (uint c=0; c<16; c++)
      p += sprintf(p,"<TD width=\"20\" align=\"center\">%02X</TD>",c);

    ostr << tmp;
    ostr << "</TR>";

    std::string color;

    uint cnt = 0;
    uint scnt = 0;
    for (uint r=0; r<rows; r++)
    {
      uint a = r*16;
      char *p = tmp;
      char *p2 = tmp2;
      p2 += sprintf(p2,"<TD bgColor=\"#C0C0C0\"></TD>");

      p += sprintf(p,"<TR><TD bgColor=\"#C0C0C0\" width=\"50\">%08X</TD>",a);
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
      p += sprintf(p,"%s</TR>\n",tmp2);
      ostr << tmp;
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





int Plugin::page_download(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
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
    int result = dataServer->getGridMessageBytes(0,toUInt32(fileIdStr),toUInt32(messageIndexStr),messageBytes,messageSections);
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
      std::vector<char> *content = new std::vector<char>();
      content->reserve(sz);

      boost::shared_ptr<std::vector<char>> sContent;
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

      char val[1000];
      sprintf(val,"attachment; filename=message_%s_%s.grib",fileIdStr.c_str(),messageIndexStr.c_str());
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





int Plugin::page_locations(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = session.getAttribute(ATTR_FILE_ID);
    std::string messageIndexStr = session.getAttribute(ATTR_MESSAGE_INDEX);
    std::string locations = session.getAttribute(ATTR_LOCATIONS);


    if (fileIdStr.empty())
      return HTTP::Status::ok;

    std::ostringstream ostr;

    ostr << "<HTML><HEAD><META charset=\"UTF-8\"></META></HEAD><BODY>\n";
    ostr << "<TABLE border=\"1\" style=\"text-align:left; font-size:10pt;\">\n";


    T::LocationFile *locationFile = getLocationFile(locations);

    if (locationFile != nullptr)
    {
      T::Coordinate_vec coordinateList = locationFile->getCoordinates();
      T::Location_vec locationList = locationFile->getLocations();
      T::GridValueList valueList;

      double_vec modificationParameters;
      if (dataServer->getGridValueListByPointList(0,toUInt32(fileIdStr),toUInt32(messageIndexStr),T::CoordinateTypeValue::LATLON_COORDINATES,coordinateList,T::AreaInterpolationMethod::Linear,0,modificationParameters,valueList) == 0)
      {
        for (auto it = locationList.begin(); it != locationList.end(); ++it)
        {
          ostr << "<TR><TD style=\"width:200;background:#F0F0F0;\">" << it->mName << "</TD>";
          T::GridValue rec;
          if (valueList.getGridValueByCoordinates(it->mX,it->mY,rec) &&  rec.mValue != ParamValueMissing)
          {
            char tmp[30];
            sprintf(tmp,"%.3f",rec.mValue);
            ostr << "<TD style=\"width:120; text-align:right;\">" << tmp << "</TD>";
          }
          else
            ostr << "<TD> </TD>";

          ostr << "</TR>";
        }
      }
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





int Plugin::page_table(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
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
    int result = dataServer->getGridData(0,toUInt32(fileIdStr),toUInt32(messageIndexStr),gridData);
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

    if (width > 100)
      width = 100;

    if (height > 100)
      height = 100;

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
        if (c < gridData.mValues.size())
        {
          if (gridData.mValues[c] != ParamValueMissing)
          {
            sprintf(tmp,"%.3f",gridData.mValues[c]);
            ostr << tmp;
          }
          else
          {
            ostr << "Null";
          }
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





int Plugin::page_coordinates(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
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
    int result = dataServer->getGridCoordinates(0,toUInt32(fileIdStr),toUInt32(messageIndexStr),T::CoordinateTypeValue::LATLON_COORDINATES,coordinates);
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

    if (width > 100)
      width = 100;

    if (height > 100)
      height = 100;

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





int Plugin::page_value(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    uint fileId = session.getUIntAttribute(ATTR_FILE_ID);
    uint messageIndex = session.getUIntAttribute(ATTR_MESSAGE_INDEX);
    float xPos = session.getDoubleAttribute(ATTR_X);
    float yPos = session.getDoubleAttribute(ATTR_Y);

    if (fileId == 0)
      return HTTP::Status::ok;

    T::ContentInfo contentInfo;
    if (contentServer->getContentInfo(0,fileId,messageIndex,contentInfo) != 0)
      return HTTP::Status::ok;

    if (contentInfo.mGeometryId == 0)
      return HTTP::Status::ok;

    uint cols = 0;
    uint rows = 0;

    if (!Identification::gridDef.getGridDimensionsByGeometryId(contentInfo.mGeometryId,cols,rows))
      return HTTP::Status::ok;

    uint height = rows;
    uint width = cols;

    double dWidth = C_DOUBLE(width);
    double dHeight = C_DOUBLE(height);

    bool reverseXDirection = false;
    bool reverseYDirection = false;

    if (!Identification::gridDef.getGridDirectionsByGeometryId(contentInfo.mGeometryId,reverseXDirection,reverseYDirection))
      return HTTP::Status::ok;
/*
    T::Coordinate_vec coordinates;
    coordinates = Identification::gridDef.getGridLatLonCoordinatesByGeometryId(contentInfo.mGeometryId);

    bool rotate = true;
    if (coordinates.size() > (10*width)  &&  coordinates[0].y() < coordinates[10*width].y())
      rotate = true;
    else
      rotate = false;
*/

    double xx = xPos * dWidth;
    double yy = yPos * dHeight;

    if (!reverseYDirection)
      yy = dHeight - (yPos * dHeight);

    if (reverseXDirection)
      xx = dWidth - (xPos * dWidth);

    T::ParamValue value = 0;
    double_vec modificationParameters;
    dataServer->getGridValueByPoint(0,fileId,messageIndex,T::CoordinateTypeValue::GRID_COORDINATES,xx,yy,T::AreaInterpolationMethod::Nearest,0,modificationParameters,value);

    if (value != ParamValueMissing)
      theResponse.setContent(std::to_string(value));
    else
      theResponse.setContent(std::string("Not available"));

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





int Plugin::page_timeseries(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    uint fileId = session.getUIntAttribute(ATTR_FILE_ID);
    uint messageIndex = session.getUIntAttribute(ATTR_MESSAGE_INDEX);
    float xPos = session.getDoubleAttribute(ATTR_X);
    float yPos = session.getDoubleAttribute(ATTR_Y);

    if (fileId == 0)
      return HTTP::Status::ok;

    T::ContentInfo contentInfo;
    if (contentServer->getContentInfo(0,fileId,messageIndex,contentInfo) != 0)
      return HTTP::Status::ok;

    if (contentInfo.mGeometryId == 0)
      return HTTP::Status::ok;

    uint cols = 0;
    uint rows = 0;

    if (!Identification::gridDef.getGridDimensionsByGeometryId(contentInfo.mGeometryId,cols,rows))
      return HTTP::Status::ok;

    uint height = rows;
    uint width = cols;

    double dWidth = C_DOUBLE(width);
    double dHeight = C_DOUBLE(height);

    double xx = xPos * dWidth;
    double yy = yPos * dHeight;

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByParameterAndGenerationId(0,contentInfo.mGenerationId,T::ParamKeyTypeValue::FMI_NAME,contentInfo.getFmiParameterName(),contentInfo.mFmiParameterLevelId,contentInfo.mParameterLevel,contentInfo.mParameterLevel,-2,-2,-2,"14000101T000000","23000101T000000",0,contentInfoList);

    contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiId_producer_generation_level_time);

    std::vector <T::ParamValue> valueList;

    int idx = -1;
    std::set<int> dayIdx;

    uint c = 0;
    uint len = contentInfoList.getLength();
    for (uint t=0; t<len; t++)
    {
      T::ContentInfo *info = contentInfoList.getContentInfoByIndex(t);

      if (info->mGeometryId == contentInfo.mGeometryId  &&  info->mForecastType == contentInfo.mForecastType  &&  info->mForecastNumber == contentInfo.mForecastNumber)
      {
        T::ParamValue value = 0;
        double_vec modificationParameters;
        if (dataServer->getGridValueByPoint(0,info->mFileId,info->mMessageIndex,T::CoordinateTypeValue::GRID_COORDINATES,xx,yy,T::AreaInterpolationMethod::Linear,0,modificationParameters,value) == 0)
        {
          if (value != ParamValueMissing)
          {
            if (info->mFileId == fileId  &&  info->mMessageIndex == messageIndex)
              idx = c;

            if (strstr(info->getForecastTime(),"T000000") != nullptr)
              dayIdx.insert(t);

            valueList.emplace_back(value);
            c++;
          }
        }
      }
    }

    char fname[200];
    sprintf(fname,"%s/grid-gui-image_%llu.jpg",itsImageCache_dir.c_str(),getTime());

    saveTimeSeries(fname,valueList,idx,dayIdx);

    long long sz = getFileSize(fname);
    if (sz > 0)
    {
      char buf[10000];
      std::vector<char> *content = new std::vector<char>();
      content->reserve(sz);

      boost::shared_ptr<std::vector<char> > sContent;
      sContent.reset(content);

      FILE *file = fopen(fname,"re");
      if (file != nullptr)
      {
        while (!feof(file))
        {
          int n = fread(buf,1,10000,file);
          if (n > 0)
          {
            for (int t=0; t<n; t++)
            {
              content->emplace_back(buf[t]);
            }
          }
        }
        fclose(file);
        remove(fname);

        theResponse.setHeader("Content-Type","image/jpg");
        theResponse.setContent(sContent);
      }
      return HTTP::Status::ok;
    }
    else
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "Image does not exist!\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return HTTP::Status::ok;
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





bool Plugin::loadImage(const char *fname,Spine::HTTP::Response &theResponse)
{
  FUNCTION_TRACE
  try
  {
    long long sz = getFileSize(fname);
    if (sz > 0)
    {
      char buf[10000];
      std::vector<char> *content = new std::vector<char>();
      content->reserve(sz);

      boost::shared_ptr<std::vector<char>> sContent;
      sContent.reset(content);

      FILE *file = fopen(fname,"re");
      if (file != nullptr)
      {
        while (!feof(file))
        {
          int n = fread(buf,1,10000,file);
          if (n > 0)
          {
            for (int t=0; t<n; t++)
            {
              content->emplace_back(buf[t]);
            }
          }
        }
        fclose(file);

        theResponse.setHeader("Content-Type","image/jpg");
        theResponse.setContent(sContent);
        return true;
      }
      return false;
    }
    else
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "Image does not exist!\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      theResponse.setHeader("Content-Type", "text/html; charset=UTF-8");
      return false;
    }
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}





int Plugin::page_image(Spine::Reactor &theReactor,
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
    std::string isolinesStr = session.getAttribute(ATTR_ISOLINES);
    std::string isolineValuesStr = session.getAttribute(ATTR_ISOLINE_VALUES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string locations = session.getAttribute(ATTR_LOCATIONS);
    std::string symbolMap = session.getAttribute(ATTR_SYMBOL_MAP);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string backgroundStr = session.getAttribute(ATTR_BACKGROUND);
    //std::string daliIdStr = session.getAttribute(ATTR_DALI_ID);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    std::string colorMapFileName = "";
    std::string colorMapModificationTime = "";
    if (!colorMap.empty() &&  strcasecmp(colorMap.c_str(),"None") != 0)
    {
      T::ColorMapFile *colorMapFile = getColorMapFile(colorMap);
      if (colorMapFile != nullptr)
      {
        colorMapModificationTime = std::to_string(C_UINT(colorMapFile->getLastModificationTime()));
        colorMapFileName = colorMapFile->getFilename();
      }
    }

    std::string hash = "Image:" + fileIdStr + ":" + messageIndexStr + ":" + hueStr + ":" + saturationStr + ":" +
      blurStr + ":" + coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + colorMapFileName + ":" + colorMapModificationTime + ":" + missingStr;

    std::size_t seed = 0;
    boost::hash_combine(seed, hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    auto etag = theRequest.getHeader("If-None-Match");
    if (etag  && *etag == seedStr)
      return HTTP::Status::not_modified;

    bool found = false;
    bool ind = true;

    time_t endTime = time(0) + 30;

    while (ind  &&  time(0) < endTime)
    {
      {
        AutoThreadLock lock(&itsThreadLock);
        auto it = itsImages.find(hash);
        if (it != itsImages.end())
        {
          // ### The requested image has been generated earlier. We can use it.

          loadImage(it->second.c_str(),theResponse);
          return HTTP::Status::ok;
        }
      }

      if (!found)
      {
        // ### Let's check if another thread is already generating the requested image

        for (uint t=0; t<100; t++)
        {
          if (itsImagesUnderConstruction[t] == hash)
          {
            found = true;
          }
        }

        if (!found)
          ind = false;
      }

      if (found)
      {
        // ### Another thread is generating the requested image. Let's wait until it is ready and can be
        // ### found from the itsImages list.

        time_usleep(0,10000);
        found = false;
      }
    }

    // ### It seems that we should generated the requested image by ourselves.

    uint idx = itsImageCounter % 100;
    itsImagesUnderConstruction[idx] = hash;
    itsImageCounter++;

    try
    {
      uint fileId = toUInt32(fileIdStr);
      uint messageIndex = toUInt32(messageIndexStr);
      int geometryId = toInt32(geometryIdStr);
      uint projectionId = toUInt32(projectionIdStr);
      uint landBorder = getColorValue(landBorderStr);
      uint coordinateLines = getColorValue(coordinateLinesStr);

      char fname[200];
      sprintf(fname,"%s/grid-gui-image_%llu.jpg",itsImageCache_dir.c_str(),getTime());

      saveImage(fname,fileId,messageIndex,toUInt8(hueStr),toUInt8(saturationStr),toUInt8(blurStr),coordinateLines,0xFFFFFFFF,"",landBorder,landMaskStr,seaMaskStr,colorMap,missingStr,geometryId,projectionId,"","",false,0,0,0,true,false);

      if (loadImage(fname,theResponse))
      {
        AutoThreadLock lock(&itsThreadLock);
        if (itsImages.find(hash) == itsImages.end())
        {
          itsImages.insert(std::pair<std::string,std::string>(hash,fname));
        }
      }
      itsImagesUnderConstruction[idx] = "";
    }
    catch (...)
    {
      itsImagesUnderConstruction[idx] = "";
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





int Plugin::page_isolines(Spine::Reactor &theReactor,
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
    std::string isolinesStr = session.getAttribute(ATTR_ISOLINES);
    std::string isolineValuesStr = session.getAttribute(ATTR_ISOLINE_VALUES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string locations = session.getAttribute(ATTR_LOCATIONS);
    std::string symbolMap = session.getAttribute(ATTR_SYMBOL_MAP);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string backgroundStr = session.getAttribute(ATTR_BACKGROUND);
    //std::string daliIdStr = session.getAttribute(ATTR_DALI_ID);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    std::string hash = "Isolines:" + fileIdStr + ":" + messageIndexStr + ":" +
      coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + isolinesStr + ":" + isolineValuesStr;

    std::size_t seed = 0;
    boost::hash_combine(seed, hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    auto etag = theRequest.getHeader("If-None-Match");
    if (etag  && *etag == seedStr)
      return HTTP::Status::not_modified;

    bool found = false;
    bool ind = true;

    while (ind)
    {
      {
        AutoThreadLock lock(&itsThreadLock);
        auto it = itsImages.find(hash);
        if (it != itsImages.end())
        {
          loadImage(it->second.c_str(),theResponse);
          return HTTP::Status::ok;
        }
      }

      if (!found)
      {
        for (uint t=0; t<100; t++)
        {
          if (itsImagesUnderConstruction[t] == hash)
          {
            found = true;
          }
        }
        if (!found)
          ind = false;
      }

      if (found)
        time_usleep(0,10000);
    }

    uint idx = itsImageCounter % 100;
    itsImagesUnderConstruction[idx] = hash;
    itsImageCounter++;

    try
    {
      uint fileId = toUInt32(fileIdStr);
      uint messageIndex = toUInt32(messageIndexStr);
      int geometryId = toInt32(geometryIdStr);
      uint projectionId = toUInt32(projectionIdStr);
      uint landBorder = getColorValue(landBorderStr);
      uint coordinateLines = getColorValue(coordinateLinesStr);
      uint isolines = getColorValue(isolinesStr);

      char fname[200];
      sprintf(fname,"%s/grid-gui-image_%llu.jpg",itsImageCache_dir.c_str(),getTime());

      saveImage(fname,fileId,messageIndex,0,0,0,coordinateLines,isolines,isolineValuesStr,landBorder,landMaskStr,seaMaskStr,"","",geometryId,projectionId,"","",false,0,0,0,true,false);

      if (loadImage(fname,theResponse))
      {
        AutoThreadLock lock(&itsThreadLock);
        if (itsImages.find(hash) == itsImages.end())
        {
          itsImages.insert(std::pair<std::string,std::string>(hash,fname));
        }
      }

      itsImagesUnderConstruction[idx] = "";
    }
    catch (...)
    {
      itsImagesUnderConstruction[idx] = "";
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





int Plugin::page_streams(Spine::Reactor &theReactor,
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
    std::string isolinesStr = session.getAttribute(ATTR_ISOLINES);
    std::string isolineValuesStr = session.getAttribute(ATTR_ISOLINE_VALUES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string locations = session.getAttribute(ATTR_LOCATIONS);
    std::string symbolMap = session.getAttribute(ATTR_SYMBOL_MAP);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string backgroundStr = session.getAttribute(ATTR_BACKGROUND);
    //std::string daliIdStr = session.getAttribute(ATTR_DALI_ID);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    std::string hash = "Streams:" + fileIdStr + ":" + messageIndexStr + ":" +
      coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr+ ":" + stepStr+ ":" + minLengthStr+ ":" + maxLengthStr + ":" + backgroundStr;

    std::size_t seed = 0;
    boost::hash_combine(seed, hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    auto etag = theRequest.getHeader("If-None-Match");
    if (etag  && *etag == seedStr)
      return HTTP::Status::not_modified;

    bool found = false;
    bool ind = true;

    while (ind)
    {
      {
        AutoThreadLock lock(&itsThreadLock);
        auto it = itsImages.find(hash);
        if (it != itsImages.end())
        {
          loadImage(it->second.c_str(),theResponse);
          return HTTP::Status::ok;
        }
      }

      if (!found)
      {
        for (uint t=0; t<100; t++)
        {
          if (itsImagesUnderConstruction[t] == hash)
          {
            found = true;
          }
        }
        if (!found)
          ind = false;
      }

      if (found)
        time_usleep(0,10000);
    }

    uint idx = itsImageCounter % 100;
    itsImagesUnderConstruction[idx] = hash;
    itsImageCounter++;

    try
    {
      uint fileId = toUInt32(fileIdStr);
      uint messageIndex = toUInt32(messageIndexStr);
      int geometryId = toInt32(geometryIdStr);
      uint projectionId = toUInt32(projectionIdStr);
      uint landBorder = getColorValue(landBorderStr);
      uint step = toUInt32(stepStr);
      uint minLength = toUInt32(minLengthStr);
      uint maxLength = toUInt32(maxLengthStr);
      bool lightBackground = true;
      if (backgroundStr == "dark")
        lightBackground = false;

      uint coordinateLines = getColorValue(coordinateLinesStr);

      char fname[200];
      sprintf(fname,"%s/grid-gui-image_%llu.jpg",itsImageCache_dir.c_str(),getTime());

      saveImage(fname,fileId,messageIndex,0,0,0,coordinateLines,0xFFFFFFFF,"",landBorder,landMaskStr,seaMaskStr,"","",geometryId,projectionId,"","",false,step,minLength,maxLength,lightBackground,false);

      if (loadImage(fname,theResponse))
      {
        AutoThreadLock lock(&itsThreadLock);
        if (itsImages.find(hash) == itsImages.end())
        {
          itsImages.insert(std::pair<std::string,std::string>(hash,fname));
        }
      }

      itsImagesUnderConstruction[idx] = "";
    }
    catch (...)
    {
      itsImagesUnderConstruction[idx] = "";
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





int Plugin::page_streamsAnimation(Spine::Reactor &theReactor,
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
    std::string isolinesStr = session.getAttribute(ATTR_ISOLINES);
    std::string isolineValuesStr = session.getAttribute(ATTR_ISOLINE_VALUES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string locations = session.getAttribute(ATTR_LOCATIONS);
    std::string symbolMap = session.getAttribute(ATTR_SYMBOL_MAP);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string backgroundStr = session.getAttribute(ATTR_BACKGROUND);
    //std::string daliIdStr = session.getAttribute(ATTR_DALI_ID);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    std::string hash = "StreamsAnimation:" + fileIdStr + ":" + messageIndexStr + ":" +
      coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr+ ":" + stepStr+ ":" + minLengthStr+ ":" + maxLengthStr + ":" + backgroundStr;

    std::size_t seed = 0;
    boost::hash_combine(seed, hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    auto etag = theRequest.getHeader("If-None-Match");
    if (etag  && *etag == seedStr)
      return HTTP::Status::not_modified;

    bool found = false;
    bool ind = true;

    while (ind)
    {
      {
        AutoThreadLock lock(&itsThreadLock);
        auto it = itsImages.find(hash);
        if (it != itsImages.end())
        {
          loadImage(it->second.c_str(),theResponse);
          return HTTP::Status::ok;
        }
      }

      if (!found)
      {
        for (uint t=0; t<100; t++)
        {
          if (itsImagesUnderConstruction[t] == hash)
          {
            found = true;
          }
        }
        if (!found)
          ind = false;
      }

      if (found)
        time_usleep(0,10000);
    }

    uint idx = itsImageCounter % 100;
    itsImagesUnderConstruction[idx] = hash;
    itsImageCounter++;

    try
    {
      uint fileId = toUInt32(fileIdStr);
      uint messageIndex = toUInt32(messageIndexStr);
      int geometryId = toInt32(geometryIdStr);
      uint projectionId = toUInt32(projectionIdStr);
      uint landBorder = getColorValue(landBorderStr);
      uint step = toUInt32(stepStr);
      uint minLength = toUInt32(minLengthStr);
      uint maxLength = toUInt32(maxLengthStr);
      bool lightBackground = true;
      if (backgroundStr == "dark")
        lightBackground = false;

      uint coordinateLines = getColorValue(coordinateLinesStr);

      char fname[200];
      sprintf(fname,"%s/grid-gui-image_%llu.webp",itsImageCache_dir.c_str(),getTime());

      saveImage(fname,fileId,messageIndex,0,0,0,coordinateLines,0xFFFFFFFF,"",landBorder,landMaskStr,seaMaskStr,"","",geometryId,projectionId,"","",false,step,minLength,maxLength,lightBackground,true);

      if (loadImage(fname,theResponse))
      {
        AutoThreadLock lock(&itsThreadLock);
        if (itsImages.find(hash) == itsImages.end())
        {
          itsImages.insert(std::pair<std::string,std::string>(hash,fname));
        }
      }

      itsImagesUnderConstruction[idx] = "";
    }
    catch (...)
    {
      itsImagesUnderConstruction[idx] = "";
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





int Plugin::page_symbols(Spine::Reactor &theReactor,
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
    std::string isolinesStr = session.getAttribute(ATTR_ISOLINES);
    std::string isolineValuesStr = session.getAttribute(ATTR_ISOLINE_VALUES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string locations = session.getAttribute(ATTR_LOCATIONS);
    std::string symbolMap = session.getAttribute(ATTR_SYMBOL_MAP);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string backgroundStr = session.getAttribute(ATTR_BACKGROUND);
    //std::string daliIdStr = session.getAttribute(ATTR_DALI_ID);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    std::string colorMapFileName = "";
    std::string colorMapModificationTime = "";

    std::string locationFileName = "";
    std::string locationFileModificationTime = "";
    if (!locations.empty() &&  strcasecmp(locations.c_str(),"None") != 0)
    {
      T::LocationFile *locationFile = getLocationFile(locations);
      if (locationFile != nullptr)
      {
        locationFileModificationTime = std::to_string(C_UINT(locationFile->getLastModificationTime()));
        locationFileName = locationFile->getFilename();
      }
    }

    std::string symbolMapFileName = "";
    std::string symbolMapModificationTime = "";
    if (!symbolMap.empty() &&  strcasecmp(symbolMap.c_str(),"None") != 0)
    {
      T::SymbolMapFile* symbolMapFile = getSymbolMapFile(symbolMap);
      if (symbolMapFile != nullptr)
      {
        symbolMapModificationTime = std::to_string(C_UINT(symbolMapFile->getLastModificationTime()));
        symbolMapFileName = symbolMapFile->getFilename();
      }
    }


    std::string hash = "Symbols:" + fileIdStr + ":" + messageIndexStr + ":" + hueStr + ":" + saturationStr + ":" +
      blurStr + ":" + coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + locationFileName + ":" + locationFileModificationTime + ":" +
      symbolMapFileName + ":" + symbolMapModificationTime;

    std::size_t seed = 0;
    boost::hash_combine(seed, hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    auto etag = theRequest.getHeader("If-None-Match");
    if (etag  && *etag == seedStr)
      return HTTP::Status::not_modified;

    bool found = false;
    bool ind = true;

    while (ind)
    {
      {
        AutoThreadLock lock(&itsThreadLock);
        auto it = itsImages.find(hash);
        if (it != itsImages.end())
        {
          loadImage(it->second.c_str(),theResponse);
          return HTTP::Status::ok;
        }
      }

      if (!found)
      {
        for (uint t=0; t<100; t++)
        {
          if (itsImagesUnderConstruction[t] == hash)
          {
            found = true;
          }
        }
        if (!found)
          ind = false;
      }

      if (found)
        time_usleep(0,10000);
    }

    uint idx = itsImageCounter % 100;
    itsImagesUnderConstruction[idx] = hash;
    itsImageCounter++;

    try
    {
      uint fileId = toUInt32(fileIdStr);
      uint messageIndex = toUInt32(messageIndexStr);
      int geometryId = toInt32(geometryIdStr);
      uint projectionId = toUInt32(projectionIdStr);
      uint landBorder = getColorValue(landBorderStr);
      uint coordinateLines = getColorValue(coordinateLinesStr);

      char fname[200];
      sprintf(fname,"/%s/grid-gui-image_%llu.jpg",itsImageCache_dir.c_str(),getTime());
      saveImage(fname,fileId,messageIndex,toUInt8(hueStr),toUInt8(saturationStr),toUInt8(blurStr),coordinateLines,0xFFFFFFFF,"",landBorder,landMaskStr,seaMaskStr,"","",geometryId,projectionId,symbolMap,locations,true,0,0,0,true,false);

      if (loadImage(fname,theResponse))
      {
        AutoThreadLock lock(&itsThreadLock);
        if (itsImages.find(hash) == itsImages.end())
        {
          itsImages.insert(std::pair<std::string,std::string>(hash,fname));
        }
      }

      itsImagesUnderConstruction[idx] = "";
    }
    catch (...)
    {
      itsImagesUnderConstruction[idx] = "";
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





int Plugin::page_map(Spine::Reactor &theReactor,
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
    std::string isolinesStr = session.getAttribute(ATTR_ISOLINES);
    std::string isolineValuesStr = session.getAttribute(ATTR_ISOLINE_VALUES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string locations = session.getAttribute(ATTR_LOCATIONS);
    std::string symbolMap = session.getAttribute(ATTR_SYMBOL_MAP);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string backgroundStr = session.getAttribute(ATTR_BACKGROUND);
    //std::string daliIdStr = session.getAttribute(ATTR_DALI_ID);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);

    std::string colorMapFileName = "";
    std::string colorMapModificationTime = "";
    if (!colorMap.empty() &&  strcasecmp(colorMap.c_str(),"None") != 0)
    {
      T::ColorMapFile *colorMapFile = getColorMapFile(colorMap);
      if (colorMapFile != nullptr)
      {
        colorMapModificationTime = std::to_string(C_UINT(colorMapFile->getLastModificationTime()));
        colorMapFileName = colorMapFile->getFilename();
      }
    }

    std::string hash = "Map:" + fileIdStr + ":" + messageIndexStr + ":" + hueStr + ":" + saturationStr + ":" +
      blurStr + ":" + coordinateLinesStr + ":" + landBorderStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + colorMapFileName + ":" + colorMapModificationTime + ":" + missingStr;

    std::size_t seed = 0;
    boost::hash_combine(seed, hash);
    std::string seedStr = std::to_string(seed);
    theResponse.setHeader("ETag",seedStr);

    auto etag = theRequest.getHeader("If-None-Match");
    if (etag  && *etag == seedStr)
      return HTTP::Status::not_modified;

    bool found = false;
    bool ind = true;

    while (ind)
    {
      {
        AutoThreadLock lock(&itsThreadLock);
        auto it = itsImages.find(hash);
        if (it != itsImages.end())
        {
          loadImage(it->second.c_str(),theResponse);
          return HTTP::Status::ok;
        }
      }

      if (!found)
      {
        for (uint t=0; t<100; t++)
        {
          if (itsImagesUnderConstruction[t] == hash)
          {
            found = true;
          }
        }
        if (!found)
          ind = false;
      }

      if (found)
        time_usleep(0,10000);
    }

    uint idx = itsImageCounter % 100;
    itsImagesUnderConstruction[idx] = hash;
    itsImageCounter++;

    try
    {
      uint columns = 1800;
      uint rows = 900;
      uint coordinateLines = getColorValue(coordinateLinesStr);

      T::ParamValue_vec values;
      double_vec modificationParameters;
      int result = dataServer->getGridValueVectorByRectangle(0,toUInt32(fileIdStr),toUInt32(messageIndexStr),T::CoordinateTypeValue::LATLON_COORDINATES,columns,rows,-180,90,360/C_DOUBLE(columns),-180/C_DOUBLE(rows),T::AreaInterpolationMethod::Nearest,0,modificationParameters,values);
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

      char fname[200];
      sprintf(fname,"/%s/grid-gui-image_%llu.jpg",itsImageCache_dir.c_str(),getTime());
      saveMap(fname,columns,rows,values,toUInt8(hueStr),toUInt8(saturationStr),toUInt8(blurStr),coordinateLines,landBorder,landMaskStr,seaMaskStr,colorMap,missingStr);

      if (loadImage(fname,theResponse))
      {
        AutoThreadLock lock(&itsThreadLock);
        if (itsImages.find(hash) == itsImages.end())
        {
          itsImages.insert(std::pair<std::string,std::string>(hash,fname));
        }
      }

      itsImagesUnderConstruction[idx] = "";
    }
    catch (...)
    {
      itsImagesUnderConstruction[idx] = "";
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





void Plugin::getLevelIds(T::ContentInfoList& contentInfoList,std::set<int>& levelIds)
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





void Plugin::getLevels(T::ContentInfoList& contentInfoList,int levelId,std::set<int>& levels)
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





void Plugin::getForecastTypes(T::ContentInfoList& contentInfoList,int levelId,int level,std::set<int>& forecastTypes)
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





void Plugin::getForecastNumbers(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,std::set<int>& forecastNumbers)
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





void Plugin::getGeometries(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,int forecastNumber,std::set<int>& geometries)
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





std::string Plugin::getFmiKey(std::string& producerName,T::ContentInfo& contentInfo)
{
  FUNCTION_TRACE
  try
  {
    char buf[200];
    char *p = buf;

    if (strlen(contentInfo.getFmiParameterName()) > 0)
      p += sprintf(p,"%s",contentInfo.getFmiParameterName());
    else
    if (contentInfo.mFmiParameterId > 0)
      p += sprintf(p,"FMI-%u",contentInfo.mFmiParameterId);

    p += sprintf(p,":%s",producerName.c_str());

    if (contentInfo.mGeometryId > 0)
      p += sprintf(p,":%u",contentInfo.mGeometryId);
    else
      p += sprintf(p,":");

    if (contentInfo.mFmiParameterLevelId > 0)
      p += sprintf(p,":%u",contentInfo.mFmiParameterLevelId);
    else
      p += sprintf(p,":");

    p += sprintf(p,":%d",contentInfo.mParameterLevel);

    if (contentInfo.mForecastType >= 0)
    {
      if (contentInfo.mForecastNumber >= 0)
        p += sprintf(p,":%d:%d",contentInfo.mForecastType,contentInfo.mForecastNumber);
      else
        p += sprintf(p,":%d",contentInfo.mForecastType);
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
    session.setAttribute(ATTR_TIME,"");
    session.setAttribute(ATTR_HUE,"128");
    session.setAttribute(ATTR_SATURATION,"60");
    session.setAttribute(ATTR_BLUR,"1");
    session.setAttribute(ATTR_COORDINATE_LINES,"Grey");
    session.setAttribute(ATTR_ISOLINES,"DarkGrey");
    session.setAttribute(ATTR_ISOLINE_VALUES,"Generated");
    session.setAttribute(ATTR_LAND_BORDER,"Grey");
    session.setAttribute(ATTR_LAND_MASK,"LightGrey");
    session.setAttribute(ATTR_SEA_MASK,"LightCyan");
    session.setAttribute(ATTR_COLOR_MAP,"None");
    session.setAttribute(ATTR_LOCATIONS,"None");
    session.setAttribute(ATTR_SYMBOL_MAP,"None");
    session.setAttribute(ATTR_MISSING,"Default");
    session.setAttribute(ATTR_STEP,"10");
    session.setAttribute(ATTR_MIN_LENGTH,"6");
    session.setAttribute(ATTR_MAX_LENGTH,"16");
    session.setAttribute(ATTR_BACKGROUND,"light");
    //session.setAttribute(ATTR_DALI_ID,"");
    session.setAttribute(ATTR_UNIT,"");
    session.setAttribute(ATTR_FMI_KEY,"");
    session.setAttribute(ATTR_X,"");
    session.setAttribute(ATTR_Y,"");
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.addParameter("Configuration file",itsConfigurationFile.getFilename());
    throw exception;
  }
}




int Plugin::page_main(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {

    //session.print(std::cout,0,0);

    auto contentServer = itsGridEngine->getContentServer_sptr();

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
    std::string isolinesStr = session.getAttribute(ATTR_ISOLINES);
    std::string isolineValuesStr = session.getAttribute(ATTR_ISOLINE_VALUES);
    std::string landBorderStr = session.getAttribute(ATTR_LAND_BORDER);
    std::string landMaskStr = session.getAttribute(ATTR_LAND_MASK);
    std::string seaMaskStr = session.getAttribute(ATTR_SEA_MASK);
    std::string colorMap = session.getAttribute(ATTR_COLOR_MAP);
    std::string locations = session.getAttribute(ATTR_LOCATIONS);
    std::string symbolMap = session.getAttribute(ATTR_SYMBOL_MAP);
    std::string missingStr = session.getAttribute(ATTR_MISSING);
    std::string stepStr = session.getAttribute(ATTR_STEP);
    std::string minLengthStr = session.getAttribute(ATTR_MIN_LENGTH);
    std::string maxLengthStr = session.getAttribute(ATTR_MAX_LENGTH);
    std::string backgroundStr = session.getAttribute(ATTR_BACKGROUND);
    //std::string daliIdStr = session.getAttribute(ATTR_DALI_ID);
    std::string unitStr = session.getAttribute(ATTR_UNIT);
    std::string fmiKeyStr = session.getAttribute(ATTR_FMI_KEY);

    if (session.findAttribute("#",ATTR_PRODUCER_ID))
    {
      generationIdStr = "";
      session.setAttribute(ATTR_GENERATION_ID,"");
    }

    if (generationIdStr.empty() || session.findAttribute("#",ATTR_GENERATION_ID))
    {
      parameterIdStr = "";
      session.setAttribute(ATTR_PARAMETER_ID,"");
    }

    if (parameterIdStr.empty() || session.findAttribute("#",ATTR_PARAMETER_ID))
    {
      levelIdStr = "";
      session.setAttribute(ATTR_LEVEL_ID,"");
    }

    if (levelIdStr.empty() || session.findAttribute("#",ATTR_LEVEL_ID))
    {
      levelStr = "";
      session.setAttribute(ATTR_LEVEL,"");
    }

    if (levelStr.empty() || session.findAttribute("#",ATTR_LEVEL))
    {
      forecastTypeStr = "";
      session.setAttribute(ATTR_FORECAST_TYPE,"");
    }

    if (forecastTypeStr.empty() || session.findAttribute("#",ATTR_FORECAST_TYPE))
    {
      forecastNumberStr = "";
      session.setAttribute(ATTR_FORECAST_NUMBER,"");
    }

    if (forecastNumberStr.empty() || session.findAttribute("#",ATTR_FORECAST_NUMBER))
    {
      geometryIdStr = "";
      session.setAttribute(ATTR_GEOMETRY_ID,"");
    }

    if (geometryIdStr.empty() || session.findAttribute("#",ATTR_GEOMETRY_ID))
    {
      timeStr = "";
      session.setAttribute(ATTR_TIME,"");
      projectionIdStr = "";
      session.setAttribute(ATTR_PROJECTION_ID,"");
      fileIdStr = "";
      session.setAttribute(ATTR_FILE_ID,"");
      messageIndexStr = "0";
      session.setAttribute(ATTR_MESSAGE_INDEX,"0");
    }
/*
    if (timeStr.empty() || session.findAttribute("#time"))
    {
      fileIdStr = "";
      messageIndexStr = "0";
      projectionIdStr = "";
    }
*/

    //uint daliId = toUInt32(daliIdStr);
    time_t requiredAccessTime = time(nullptr) + 120;

    if (getFileModificationTime(itsColorFile.c_str()) != itsColors_lastModified)
    {
      loadColorFile();
    }

    /*
    if (getFileModificationTime(itsDaliFile.c_str()) != itsDaliFile_lastModified)
    {
      loadDaliFile();
    }
    */

    std::ostringstream output;
    std::ostringstream ostr1;
    std::ostringstream ostr2;
    std::ostringstream ostr3;

    output << "<HTML>\n";
    output << "<BODY>\n";

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

    output << "function getImageCoords(event,img,fileId,messageIndex,presentation) {\n";
    output << "  var posX = event.offsetX?(event.offsetX):event.pageX-img.offsetLeft;\n";
    output << "  var posY = event.offsetY?(event.offsetY):event.pageY-img.offsetTop;\n";
    output << "  var prosX = posX / img.width;\n";
    output << "  var prosY = posY / img.height;\n";
    output << "  var url = \"/grid-gui?page=value&" << ATTR_PRESENTATION << "=\" + presentation + \"&" << ATTR_FILE_ID << "=\" + fileId + \"&" << ATTR_MESSAGE_INDEX << "=\" + messageIndex + \"&" << ATTR_X << "=\" + prosX + \"&" << ATTR_Y << "=\" + prosY;\n";
    output << "  var txt = httpGet(url);\n";
    output << "  document.getElementById('gridValue').value = txt;\n";

    output << "}\n";
    output << "</SCRIPT>\n";



    /*
    string_vec daliNames;
    std::map<uint,std::string> daliUrls;
    std::vector<uint> daliIds;

    for (auto it = itsDaliProducts.begin(); it != itsDaliProducts.end(); ++it)
    {
      if ((*it)[0] ==  parameterIdStr)
      {
        if ((*it)[1] == levelIdStr  ||  (*it)[1] == "")
        {
          if ((*it)[2] == levelStr  ||  (*it)[2] == "")
          {
            uint id = toUInt32((*it)[3]);
            if (daliId == 0)
              daliId = id;
            daliIds.emplace_back(id);
            daliNames.emplace_back((*it)[4]);
            daliUrls.insert(std::pair<uint,std::string>(id,(*it)[5]));
          }
        }
      }
    }

    std::string daliUrl;

    if (presentation == "Dali"  &&  daliIds.size() == 0)
    {
      presentation = "Image";
      daliId = 0;
    }
    */

    ostr1 << "<TABLE width=\"100%\" height=\"100%\">\n";


    // ### Producers:

    T::ProducerInfoList producerInfoList;
    contentServer->getProducerInfoList(0,producerInfoList);
    uint len = producerInfoList.getLength();
    producerInfoList.sortByName();
    uint producerId = toUInt32(producerIdStr);

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Producer:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PRODUCER_ID << "=' + this.options[this.selectedIndex].value)\">\n";
      for (uint t=0; t<len; t++)
      {
        T::ProducerInfo *p = producerInfoList.getProducerInfoByIndex(t);

        if (producerId == 0)
        {
          producerId = p->mProducerId;
          producerIdStr = std::to_string(producerId);
          //printf("EMPTY => PRODUCER %s\n",producerIdStr.c_str());
        }

        if (producerId == p->mProducerId)
        {
          producerNameStr = p->mName;
          session.setAttribute(ATTR_PRODUCER_ID,producerId);
          session.setAttribute(ATTR_PRODUCER_NAME,producerNameStr);
          ostr1 << "<OPTION selected value=\"" <<  p->mProducerId << "\">" <<  p->mName << "</OPTION>\n";
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  p->mProducerId << "\">" <<  p->mName << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";


    // ### Generations:

    T::GenerationInfoList generationInfoList;
    T::GenerationInfoList generationInfoList2;
    contentServer->getGenerationInfoListByProducerId(0,producerId,generationInfoList2);
    generationInfoList2.getGenerationInfoListByProducerId(producerId,generationInfoList);
    //generationInfoList2.getGenerationInfoListByProducerIdAndStatus(producerId,generationInfoList,T::GenerationInfo::Status::Ready);

    uint generationId = toUInt32(generationIdStr);

    if (generationInfoList.getGenerationInfoById(generationId) == nullptr)
      generationId = 0;

    std::set<std::string> generations;
    getGenerations(generationInfoList,generations);
    std::string originTime;


    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Generation:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    if (generations.size() > 0)
    {
      std::string disabled = "";
      if (generations.size() == 1)
        disabled = "disabled";

      ostr1 << "<SELECT style=\"width:250px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_GENERATION_ID << "=' + this.options[this.selectedIndex].value)\">\n";

      for (auto it = generations.rbegin(); it != generations.rend(); ++it)
      {
        std::string name = *it;
        T::GenerationInfo *g = generationInfoList.getGenerationInfoByName(name);
        if (g != nullptr && (g->mDeletionTime == 0 || g->mDeletionTime > requiredAccessTime))
        {
          if (generationId == 0)
          {
            generationId = g->mGenerationId;
            generationIdStr = std::to_string(generationId);
            //printf("EMPTY => GENERATION %s\n",generationIdStr.c_str());
          }

          if (generationId == g->mGenerationId)
          {
            originTime = g->mAnalysisTime;
            ostr1 << "<OPTION selected value=\"" <<  g->mGenerationId << "\">" <<  g->mName << "</OPTION>\n";
            session.setAttribute(ATTR_GENERATION_ID,generationId);
          }
          else
            ostr1 << "<OPTION value=\"" <<  g->mGenerationId << "\">" <<  g->mName << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";


    // ### Parameters:

    std::string paramDescription;
    std::set<std::string> paramKeyList;
    contentServer->getContentParamKeyListByGenerationId(0,generationId,T::ParamKeyTypeValue::FMI_NAME,paramKeyList);

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Parameter:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    if (paramKeyList.size() > 0)
    {
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PARAMETER_ID << "=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it=paramKeyList.begin(); it!=paramKeyList.end(); ++it)
      {
        std::string parameterId = *it;
        std::string pName = *it;

        char st[100];
        strcpy(st,it->c_str());

        if (strncasecmp(st,"GRIB-",5) == 0)
        {
          std::string key = st+5;

          Identification::GribParameterDef  def;
          if (Identification::gridDef.getGribParameterDefById(toUInt32(key),def))
          {
            pName = *it + " (" + def.mParameterDescription + ")";
          }
        }
        else
        if (strncasecmp(st,"NB-",3) == 0)
        {
          std::string key = st+3;

          Identification::NewbaseParameterDef  def;
          if (Identification::gridDef.getNewbaseParameterDefById(toUInt32(key),def))
          {
            pName = *it + " (" + def.mParameterName + ")";
          }
        }
        else
        if (strncasecmp(st,"FMI-",4) == 0)
        {
          std::string key = st+4;

          Identification::FmiParameterDef def;
          if (Identification::gridDef.getFmiParameterDefById(toUInt32(key),def))
          {
            parameterId = def.mParameterName;
            pName = def.mParameterName + " (" + def.mParameterDescription + ")";
            if (parameterIdStr == parameterId || parameterIdStr.empty())
              unitStr = def.mParameterUnits;
          }
        }
        else
        {
          Identification::FmiParameterDef def;
          if (Identification::gridDef.getFmiParameterDefByName(*it,def))
          {
            parameterId = def.mParameterName;
            pName = def.mParameterName + " (" + def.mParameterDescription + ")";
            if (parameterIdStr == parameterId || parameterIdStr.empty())
              unitStr = def.mParameterUnits;
          }
        }


        if (parameterIdStr.empty())
        {
          parameterIdStr = parameterId;
          //printf("EMPTY => PARAMETER %s\n",parameterIdStr.c_str());
        }

        if (parameterIdStr == parameterId)
        {
          ostr1 << "<OPTION selected value=\"" <<  parameterId << "\">" <<  pName << "</OPTION>\n";
          session.setAttribute(ATTR_PARAMETER_ID,parameterId);
          paramDescription = pName;
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  parameterId << "\">" <<  pName << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";


    // ### Level identifiers:

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByParameterAndGenerationId(0,generationId,T::ParamKeyTypeValue::FMI_NAME,parameterIdStr,-1,0,0,-2,-2,-2,"14000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    int levelId = toInt32(levelIdStr);

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Level type:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    std::set<int> levelIds;
    getLevelIds(contentInfoList,levelIds);

    if (levelIds.find(levelId) == levelIds.end())
      levelId = -1;

    if (levelIds.size() > 0)
    {
      std::string disabled = "";
      if (levelIds.size() == 1)
        disabled = "disabled";

      ostr1 << "<SELECT style=\"width:250px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LEVEL_ID << "=' + this.options[this.selectedIndex].value) \">\n";
      for (auto it = levelIds.begin(); it != levelIds.end(); ++it)
      {
        if (levelIdStr.empty())
        {
          levelIdStr = std::to_string(*it);
          levelId = *it;
          //printf("EMPTY => LEVEL ID %s\n",levelIdStr.c_str());
        }

        std::string lStr = std::to_string(*it);
        Identification::LevelDef levelDef;
        if (*it < 1000)
        {
          if (Identification::gridDef.getFmiLevelDef(*it,levelDef))
            lStr = "FMI-" + std::to_string(*it) + " : " + levelDef.mDescription;
          else
            lStr = "FMI-" + std::to_string(*it) + " : ";
        }
        /*
        else
        if (*it < 2000)
        {
          if (Identification::gridDef.getGrib1LevelDef(*it % 1000,levelDef))
            lStr = "GRIB1-" + std::to_string(*it % 1000) + " : " + levelDef.mDescription;
          else
            lStr = "GRIB1-" + std::to_string(*it % 1000) + " : ";
        }
        else
        {
          if (Identification::gridDef.getGrib2LevelDef(*it % 1000,levelDef))
            lStr = "GRIB2-" + std::to_string(*it % 1000) + " : " + levelDef.mDescription;
          else
            lStr = "GRIB2-" + std::to_string(*it % 1000) + " : ";
        }
*/
        if (levelId == *it)
        {
          ostr1 << "<OPTION selected value=\"" <<  *it << "\">" <<  lStr << "</OPTION>\n";
          session.setAttribute(ATTR_LEVEL_ID,levelId);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  *it << "\">" <<  lStr << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";


    // ### Levels:

    contentInfoList.clear();
    contentServer->getContentListByParameterAndGenerationId(0,generationId,T::ParamKeyTypeValue::FMI_NAME,parameterIdStr,levelId,0,0x7FFFFFFF,-2,-2,-2,"14000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    T::ParamLevel level = toInt32(levelStr);

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Level:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    std::set<int> levels;
    getLevels(contentInfoList,levelId,levels);

    if (levels.find(level) == levels.end())
      level = 0;

    if (levels.size() > 0)
    {
      std::string disabled = "";
      if (levels.size() == 1)
        disabled = "disabled";

      ostr1 << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LEVEL << "=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it = levels.begin(); it != levels.end(); ++it)
      {
        if (levelStr.empty())
        {
          levelStr = std::to_string(*it);
          level = *it;
          //printf("EMPTY => LEVEL %s\n",levelStr.c_str());
        }

        if (level == *it)
        {
          ostr1 << "<OPTION selected value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
          session.setAttribute(ATTR_LEVEL,level);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";


    // ### Forecast type:

    short forecastType = toInt16(forecastTypeStr);

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Forecast type</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    std::set<int> forecastTypes;
    getForecastTypes(contentInfoList,levelId,level,forecastTypes);

    if (forecastTypes.find(forecastType) == forecastTypes.end())
      forecastType = 0;

    if (forecastTypes.size() > 0)
    {
      std::string disabled = "";
      if (forecastTypes.size() == 1)
        disabled = "disabled";

      ostr1 << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_FORECAST_TYPE << "=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it = forecastTypes.begin(); it != forecastTypes.end(); ++it)
      {
        if (forecastTypeStr.empty())
        {
          forecastTypeStr = std::to_string(*it);
          forecastType = *it;
          //printf("EMPTY => FORECAST TYPE %s\n",forecastTypeStr.c_str());
        }

        std::string lStr = std::to_string(*it);
        Identification::ForecastTypeDef typeDef;
        if (Identification::gridDef.getFmiForecastTypeDef(*it,typeDef))
          lStr = std::to_string(*it) + " : " + typeDef.mName;
        else
          lStr = std::to_string(*it) + " : ";

        if (forecastType == *it)
        {
          ostr1 << "<OPTION selected value=\"" <<  *it << "\">" <<  lStr << "</OPTION>\n";
          session.setAttribute(ATTR_FORECAST_TYPE,forecastType);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  *it << "\">" << lStr << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";


    // ### Forecast number:

    short forecastNumber = toInt16(forecastNumberStr);

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Forecast number</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    std::set<int> forecastNumbers;
    getForecastNumbers(contentInfoList,levelId,level,forecastType,forecastNumbers);

    if (forecastNumbers.find(forecastNumber) == forecastNumbers.end())
      forecastNumber = 0;

    if (forecastNumbers.size() > 0)
    {
      std::string disabled = "";
      if (forecastNumbers.size() == 1)
        disabled = "disabled";

      ostr1 << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_FORECAST_NUMBER << "=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it = forecastNumbers.begin(); it != forecastNumbers.end(); ++it)
      {
        if (forecastNumberStr.empty())
        {
          forecastNumberStr = std::to_string(*it);
          forecastNumber = *it;
          //printf("EMPTY => FORECAST NUMBER %s\n",forecastNumberStr.c_str());
        }

        if (*it == forecastNumber)
        {
          ostr1 << "<OPTION selected value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
          session.setAttribute(ATTR_FORECAST_NUMBER,forecastNumber);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";

    // ### Geometries:

    T::GeometryId geometryId  = toInt32(geometryIdStr);

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Geometry:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    std::set<int> geometries;
    getGeometries(contentInfoList,levelId,level,forecastType,forecastNumber,geometries);

    if (geometries.find(geometryId) == geometries.end())
      geometryId = 0;

    if (geometries.size() > 0)
    {
      std::string disabled = "";
      if (geometries.size() == 1)
        disabled = "disabled";

      ostr1 << "<SELECT style=\"width:250px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_GEOMETRY_ID << "=' + this.options[this.selectedIndex].value)\">\n";

      for (auto it=geometries.begin(); it!=geometries.end(); ++it)
      {
        std::string st = std::to_string(*it);

        std::string gName;
        uint cols = 0;
        uint rows = 0;

        Identification::gridDef.getGeometryNameById(*it,gName);

        if (Identification::gridDef.getGridDimensionsByGeometryId(*it,cols,rows))
          st = std::to_string(*it) + ":" + gName + " (" + std::to_string(cols) + " x " + std::to_string(rows) + ")";
        else
          st = std::to_string(*it) + ":" + gName;

        if (geometryId == 0)
        {
          geometryId = *it;
          geometryIdStr = std::to_string(geometryId);
          //printf("EMPTY => GEOMETRY %s\n",geometryIdStr.c_str());
        }

        if (geometryId == *it)
        {
          ostr1 << "<OPTION selected value=\"" <<  *it << "\">" <<  st << "</OPTION>\n";
          session.setAttribute(ATTR_GEOMETRY_ID,geometryId);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  *it << "\">" <<  st << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";


    if (projectionIdStr.empty())
    {
      projectionIdStr = geometryIdStr;
      session.setAttribute(ATTR_PROJECTION_ID,projectionIdStr);
    }


    // ### Times:

    contentInfoList.clear();
    contentServer->getContentListByParameterAndGenerationId(0,generationId,T::ParamKeyTypeValue::FMI_NAME,parameterIdStr,levelId,level,level,-2,-2,-2,"14000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    std::string prevTime = "14000101T0000";

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Time (UTC):</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD><TABLE><TR><TD>\n";

    ostr3 << "<TABLE style=\"border-width:0;border-spacing:0;height:30;\"><TR>\n";

    T::ContentInfo *prevCont = nullptr;
    T::ContentInfo *currentCont = nullptr;
    T::ContentInfo *nextCont = nullptr;

    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiName_producer_generation_level_time);

      ostr1 << "<SELECT id=\"timeselect\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() + "' + this.options[this.selectedIndex].value)\"";

      std::string u;
      if (presentation == "Image" ||  presentation == "Map"  ||  presentation == "Symbols"  ||  presentation == "Isolines"  ||  presentation == "Streams")
      {
        u = "/grid-gui?session=" + session.getUrlParameter() + "&" + ATTR_PAGE + "=" + presentation;
      }

      /*
      if (presentation == "Dali"  &&  daliId > 0)
      {
        auto dUrl = daliUrls.find(daliId);
        if (dUrl != daliUrls.end())
        {
          u = dUrl->second + "&producer="+producerNameStr+"&origintime=" + originTime + "&geometryId=" + std::to_string(geometryId)+ "&levelId=" + std::to_string(levelId)+ "&level=" + std::to_string(level)   + "&forecastType=" + std::to_string(forecastType) + "&forecastNumber=" + std::to_string(forecastNumber)+"&type=png";
          u = stringReplaceAll(u,"$(PARAMETER)",parameterIdStr);
          daliUrl = u;
        }
      }
      */

      ostr1 << " >\n";

      uint tCount = 0;
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (g->mGeometryId == geometryId)
        {
          if (prevTime < g->getForecastTime())
          {
            if (forecastType == g->mForecastType)
            {
              if (forecastNumber == g->mForecastNumber)
              {
                tCount++;
              }
            }
          }
        }
      }


      std::string pTime = timeStr;

      uint daySwitch = 0;
      uint cc = 0;
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        //printf("**** TIME %s (%s)  geom=%u %u  ft=%u %u   fn=%d %d \n",g->getForecastTime(),prevTime.c_str(),g->mGeometryId,geometryId,forecastType,g->mForecastType,forecastNumber,g->mForecastNumber);

        if (g->mGeometryId == geometryId)
        {
          if (prevTime < g->getForecastTime())
          {
            if (forecastType == g->mForecastType)
            {
              if (forecastNumber == g->mForecastNumber)
              {
                std::ostringstream out;

                out << "&" << ATTR_TIME << "=" << g->getForecastTime() << "&" << ATTR_FILE_ID << "=" << g->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr;
                std::string url = out.str();
                std::string uu = url;

                //if (presentation == "Dali")
                //  uu = "&start=" + std::string(g->getForecastTime());

                if (currentCont != nullptr  &&  nextCont == nullptr)
                  nextCont = g;

                if (timeStr.empty())
                {
                  timeStr = g->getForecastTime();
                  prevTime = timeStr;
                  //printf("EMPTY => TIME %s\n",timeStr.c_str());
                }

                std::string bg = "#E0E0E0";

                if (strncmp(g->getForecastTime(),pTime.c_str(),8) != 0)
                  daySwitch++;

                if ((daySwitch % 2) == 1)
                  bg = "#D0D0D0";

                if (timeStr == g->getForecastTime())
                  bg = "#0000FF";

                if (tCount < 124  ||  (g->getForecastTime() >= timeStr  &&  cc < 124))
                {
                  if (cc == 0)
                    ostr3 << "<TD style=\"text-align:center; font-size:12;width:120;background:#F0F0F0;\" id=\"ftime\">" + timeStr + "</TD><TD style=\"width:1;\"> </TD>\n";

                  if (u > " ")
                    ostr3 << "<TD style=\"width:5; background:"+bg+";\" onmouseout=\"this.style='width:5;background:"+bg+";'\" onmouseover=\"this.style='width:5;height:30;background:#FF0000;'; setText('ftime','" + g->getForecastTime() + "');setImage(document.getElementById('myimage'),'" + u + uu + "');\" > </TD>\n";
                  else
                    ostr3 << "<TD style=\"width:5; background:"+bg+";\"> </TD>\n";

                  prevTime = g->getForecastTime();
                  cc++;
                }

                if (timeStr == g->getForecastTime())
                {
                  //printf("## SELECTED TIME  [%s][%s]\n",timeStr.c_str(),g->getForecastTime());
                  ostr1 << "<OPTION selected value=\"" <<  url << "\">" <<  g->getForecastTime() << "</OPTION>\n";
                  currentCont = g;
                  fmiKeyStr = getFmiKey(producerNameStr,*g);
                  fileIdStr = std::to_string(g->mFileId);
                  messageIndexStr = std::to_string(g->mMessageIndex);

                  session.setAttribute(ATTR_FMI_KEY,fmiKeyStr);
                  session.setAttribute(ATTR_FILE_ID,g->mFileId);
                  session.setAttribute(ATTR_MESSAGE_INDEX,g->mMessageIndex);
                  session.setAttribute(ATTR_TIME,g->getForecastTime());

                }
                else
                {
                  ostr1 << "<OPTION value=\"" <<  url << "\">" <<  g->getForecastTime() << "</OPTION>\n";
                }

                if (currentCont == nullptr)
                  prevCont = g;

                pTime = g->getForecastTime();
              }
            }
          }
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD>\n";

    if (prevCont != nullptr)
      ostr1 << "<TD width=\"20\" > <button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_TIME << "=" << prevCont->getForecastTime() << "&" << ATTR_FILE_ID << "=" << prevCont->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << prevCont->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr << "');\">&lt;</button></TD>\n";
    else
      ostr1 << "<TD width=\"20\"><button type=\"button\">&lt;</button></TD></TD>\n";

    if (nextCont != nullptr)
      ostr1 << "<TD width=\"20\"><button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_TIME << "=" << nextCont->getForecastTime() << "&" << ATTR_FILE_ID << "=" << nextCont->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << nextCont->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr + "');\">&gt;</button></TD>\n";
    else
      ostr1 << "<TD width=\"20\"><button type=\"button\">&gt;</button></TD></TD>\n";

    ostr1 << "</TR></TABLE></TD></TR>\n";

    ostr3 << "<TD></TD></TR></TABLE>\n";

    // ### Presentation:

    const char *modes[] = {"Image","Map","Isolines","Streams","StreamsAnimation","Symbols","Locations","Info","Table(sample)","Coordinates(sample)","Message",nullptr};

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Presentation:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";
    ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_TIME << "=" << timeStr << "&" << ATTR_FILE_ID << "=" << fileIdStr << "&" << ATTR_MESSAGE_INDEX << "=" << messageIndexStr << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr << "&" << ATTR_PRESENTATION << "=' + this.options[this.selectedIndex].value)\">\n";

    uint a = 0;
    while (modes[a] != nullptr)
    {
      if (presentation.empty())
      {
        presentation = modes[a];
        //printf("EMPTY => PRESENTATION %s\n",presentation.c_str());
      }

      if (presentation == modes[a])
      {
        ostr1 << "<OPTION selected value=\"" <<  modes[a] << "\">" <<  modes[a] << "</OPTION>\n";
        session.setAttribute(ATTR_PRESENTATION,presentation);
      }
      else
      {
        ostr1 << "<OPTION value=\"" <<  modes[a] << "\">" <<  modes[a] << "</OPTION>\n";
      }

      a++;
    }

    /*
    if (daliIds.size() > 0)
    {
      if (presentation == "Dali")
      {
        ostr1 << "<OPTION selected value=\"Dali\">Dali</OPTION>\n";
        session.setAttribute(ATTR_PRESENTATION,presentation);
      }
      else
      {
        ostr1 << "<OPTION value=\"Dali\">Dali</OPTION>\n";
      }
    }
    */

    ostr1 << "</SELECT>\n";
    ostr1 << "</TD></TR>\n";



    if (presentation == "Image" ||  presentation == "Symbols"  ||  presentation == "Isolines")
    {
      // ### Projections:

      std::set<T::GeometryId> projections;
      Identification::gridDef.getGeometryIdList(projections);

      T::GeometryId projectionId  = toInt32(projectionIdStr);

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Projection:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";

      if (projectionId == 0)
      {
        projectionId = geometryId;
      }

      if (projections.find(projectionId) == geometries.end())
        projectionId = geometryId;

      if (projections.size() > 0)
      {
        ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PROJECTION_ID << "=' + this.options[this.selectedIndex].value)\">\n";

        for (auto it=projections.begin(); it!=projections.end(); ++it)
        {
          std::string st = std::to_string(*it);

          std::string gName;
          uint cols = 0;
          uint rows = 0;

          Identification::gridDef.getGeometryNameById(*it,gName);

          if (Identification::gridDef.getGridDimensionsByGeometryId(*it,cols,rows))
            st = std::to_string(*it) + ":" + gName + " (" + std::to_string(cols) + " x " + std::to_string(rows) + ")";
          else
            st = std::to_string(*it) + ":" + gName;

          if (projectionId == 0)
          {
            projectionId = *it;
            projectionIdStr = std::to_string(projectionId);
            //printf("EMPTY => PROJECTION %s\n",projectionIdStr.c_str());
          }

          if (projectionId == *it)
          {
            ostr1 << "<OPTION selected value=\"" <<  *it << "\">" <<  st << "</OPTION>\n";
            session.setAttribute(ATTR_PROJECTION_ID,projectionId);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  *it << "\">" <<  st << "</OPTION>\n";
          }
        }
        ostr1 << "</SELECT>\n";
      }
      ostr1 << "</TD></TR>\n";
    }


    if (presentation == "Image" ||  presentation == "Map")
    {
      // ### Color maps:

      std::set<std::string> names;

      for (auto it = itsColorMapFiles.begin(); it != itsColorMapFiles.end(); ++it)
      {
        string_vec nameList = it->getNames();
        for (auto name = nameList.begin(); name != nameList.end(); ++name)
          names.insert(*name);
      }

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Color map:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_COLOR_MAP << "=' + this.options[this.selectedIndex].value)\">\n";

      if (colorMap.empty() ||  colorMap == "None")
      {
        ostr1 << "<OPTION selected value=\"None\">None</OPTION>\n";
        session.setAttribute(ATTR_COLOR_MAP,"None");
      }
      else
      {
        ostr1 << "<OPTION value=\"None\">None</OPTION>\n";
      }

      for (auto it = names.begin(); it != names.end(); ++it)
      {
        if (colorMap == *it)
        {
          ostr1 << "<OPTION selected value=\"" << *it << "\">" <<  *it << "</OPTION>\n";
          session.setAttribute(ATTR_COLOR_MAP,colorMap);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
      ostr1 << "</TD></TR>\n";
    }

    if (presentation == "Symbols")
    {
      // ### Symbol groups:

      std::set<std::string> groups;

      for (auto it = itsSymbolMapFiles.begin(); it != itsSymbolMapFiles.end(); ++it)
      {
        string_vec nameList = it->getNames();
        for (auto name = nameList.begin(); name != nameList.end(); ++name)
          groups.insert(*name);
      }

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Symbol group:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SYMBOL_MAP << "=' + this.options[this.selectedIndex].value)\">\n";

      if (symbolMap.empty() || symbolMap == "None")
      {
        ostr1 << "<OPTION selected value=\"None\">None</OPTION>\n";
        session.setAttribute(ATTR_SYMBOL_MAP,"None");
      }
      else
        ostr1 << "<OPTION value=\"None\">None</OPTION>\n";

      for (auto it = groups.begin(); it != groups.end(); ++it)
      {
        if (symbolMap == *it)
        {
          ostr1 << "<OPTION selected value=\"" << *it << "\">" <<  *it << "</OPTION>\n";
          session.setAttribute(ATTR_SYMBOL_MAP,symbolMap);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
      ostr1 << "</TD></TR>\n";

    }

    /*
    if (presentation == "Dali")
    {
      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Products:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_DALI_ID << "=' + this.options[this.selectedIndex].value)\">\n";

      uint dlen = daliIds.size();
      for (uint t=0; t<dlen; t++)
      {
        if (daliId == 0)
          daliId = daliIds[t];

        if (daliIds[t] == daliId)
        {
          ostr1 << "<OPTION selected value=\"" << daliIds[t] << "\">" <<  daliNames[t] << "</OPTION>\n";
          session.setAttribute(ATTR_DALI_ID,daliId);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  daliIds[t] << "\">" <<  daliNames[t] << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
      ostr1 << "</TD></TR>\n";
    }
    */


    if (presentation == "Symbols" ||  presentation == "Locations")
    {
      // ### Locations:

      std::set<std::string> names;

      for (auto it = itsLocationFiles.begin(); it != itsLocationFiles.end(); ++it)
      {
        string_vec nameList = it->getNames();
        for (auto name = nameList.begin(); name != nameList.end(); ++name)
          names.insert(*name);
      }

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Locations:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LOCATIONS << "=' + this.options[this.selectedIndex].value)\">\n";

      if (presentation == "Symbols")
      {
        if (locations.empty() ||  locations == "None")
        {
          ostr1 << "<OPTION selected value=\"None\">None</OPTION>\n";
          session.setAttribute(ATTR_LOCATIONS,"None");
        }
        else
        {
          ostr1 << "<OPTION value=\"None\">None</OPTION>\n";
        }
      }

      for (auto it = names.begin(); it != names.end(); ++it)
      {
        if (presentation == "Locations"  &&  (locations.empty() ||  locations == "None"))
          locations = *it;

        if (locations == *it)
        {
          ostr1 << "<OPTION selected value=\"" << *it << "\">" <<  *it << "</OPTION>\n";
          session.setAttribute(ATTR_LOCATIONS,locations);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
      ostr1 << "</TD></TR>\n";
    }


    if (presentation == "Image" || presentation == "Map" || presentation == "Symbols" || presentation == "Isolines" || presentation == "Streams" || presentation == "StreamsAnimation")
    {
      if ((colorMap.empty() || colorMap == "None") &&  presentation != "Symbols"  &&  presentation != "Isolines"  &&   presentation != "Streams" && presentation != "StreamsAnimation")
      {
        // ### Hue, saturation, blur

        ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Hue, saturation and blur</TD></TR>\n";
        ostr1 << "<TR height=\"30\"><TD>\n";
        ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_HUE << "=' + this.options[this.selectedIndex].value)\">\n";


        uint hue = toUInt32(hueStr);
        for (uint a=0; a<256; a++)
        {
          if (a == hue)
          {
            ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
            session.setAttribute(ATTR_HUE,a);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          }
        }
        ostr1 << "</SELECT>\n";


        uint saturation = toUInt32(saturationStr);
        ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SATURATION << "=' + this.options[this.selectedIndex].value)\">\n";


        for (uint a=0; a<256; a++)
        {
          if (a == saturation)
          {
            ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
            session.setAttribute(ATTR_SATURATION,a);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          }
        }
        ostr1 << "</SELECT>\n";


        ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_BLUR << "=' + this.options[this.selectedIndex].value)\">\n";

        uint blur = toUInt32(blurStr);
        for (uint a=1; a<=200; a++)
        {
          if (a == blur)
          {
            ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
            session.setAttribute(ATTR_BLUR,a);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          }
        }
        ostr1 << "</SELECT>\n";
        ostr1 << "</TD></TR>\n";
      }




      if (presentation == "Streams" || presentation == "StreamsAnimation")
      {
        // ### step, minLength, maxLength, background

        ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Step, min and max length, background</TD></TR>\n";
        ostr1 << "<TR height=\"30\"><TD>\n";
        ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_STEP << "=' + this.options[this.selectedIndex].value)\">\n";

        uint step = toUInt32(stepStr);
        for (uint a=2; a<100; a = a + 2)
        {
          if (a == step)
          {
            ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
            session.setAttribute(ATTR_STEP,a);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          }
        }
        ostr1 << "</SELECT>\n";


        uint minLength = toUInt32(minLengthStr);
        ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_MIN_LENGTH << "=' + this.options[this.selectedIndex].value)\">\n";

        for (uint a=2; a<200; a=a+2)
        {
          if (a == minLength)
          {
            ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
            session.setAttribute(ATTR_MIN_LENGTH,a);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          }
        }
        ostr1 << "</SELECT>\n";


        ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_MAX_LENGTH << "=' + this.options[this.selectedIndex].value)\">\n";

        uint maxLength = toUInt32(maxLengthStr);
        for (uint a=8; a<=512; a=a+4)
        {
          if (a == maxLength)
          {
            ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
            session.setAttribute(ATTR_MAX_LENGTH,a);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          }
        }
        ostr1 << "</SELECT>\n";


        ostr1 << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_BACKGROUND << "=' + this.options[this.selectedIndex].value)\">\n";

        if (backgroundStr == "dark")
        {
          ostr1 << "<OPTION selected value=\"dark\">dark</OPTION>\n";
          session.setAttribute(ATTR_BACKGROUND,"dark");
        }
        else
        {
          ostr1 << "<OPTION value=\"dark\">dark</OPTION>\n";
        }

        if (backgroundStr == "light")
        {
          ostr1 << "<OPTION selected value=\"light\">light</OPTION>\n";
          session.setAttribute(ATTR_BACKGROUND,"light");
        }
        else
        {
          ostr1 << "<OPTION value=\"light\">light</OPTION>\n";
        }

        ostr1 << "</SELECT>\n";
        ostr1 << "</TD></TR>\n";
      }



      if (presentation == "Isolines")
      {
        // ### Isoline values:

        ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Isoline values:</TD></TR>\n";
        ostr1 << "<TR height=\"30\"><TD>\n";
        ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_ISOLINE_VALUES << "=' + this.options[this.selectedIndex].value)\">\n";

        if (isolinesStr == "Generated")
        {
          ostr1 << "<OPTION selected value=\"Generated\">Generated</OPTION>\n";
          session.setAttribute(ATTR_ISOLINES,"Generated");
        }
        else
        {
          ostr1 << "<OPTION value=\"Simple\">Generated</OPTION>\n";
        }

        for (auto it = itsIsolines.begin(); it != itsIsolines.end(); ++it)
        {
          if (isolineValuesStr == it->first)
          {
            ostr1 << "<OPTION selected value=\"" << it->first << "\">" <<  it->first << "</OPTION>\n";
            session.setAttribute(ATTR_ISOLINE_VALUES,isolineValuesStr);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  it->first << "\">" <<  it->first << "</OPTION>\n";
          }

          a++;
        }
        ostr1 << "</SELECT>\n";
        ostr1 << "</TD></TR>\n";


        // ### Isoline color:

        ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Isoline color:</TD></TR>\n";
        ostr1 << "<TR height=\"30\"><TD>\n";
        ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_ISOLINES << "=' + this.options[this.selectedIndex].value)\"";

        for (auto it = itsColors.begin(); it != itsColors.end(); ++it)
        {
          if (isolinesStr == it->first)
          {
            ostr1 << "<OPTION selected value=\"" << it->first << "\">" <<  it->first << "</OPTION>\n";
            session.setAttribute(ATTR_ISOLINES,isolinesStr);
          }
          else
          {
            ostr1 << "<OPTION value=\"" <<  it->first << "\">" <<  it->first << "</OPTION>\n";
          }
          a++;
        }
        ostr1 << "</SELECT>\n";
        ostr1 << "</TD></TR>\n";
      }


      // ### Coordinate lines:

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Coordinate lines:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_COORDINATE_LINES << "=' + this.options[this.selectedIndex].value)\">\n";

      for (auto it = itsColors.begin(); it != itsColors.end(); ++it)
      {
        if (coordinateLinesStr == it->first)
        {
          ostr1 << "<OPTION selected value=\"" << it->first << "\">" <<  it->first << "</OPTION>\n";
          session.setAttribute(ATTR_COORDINATE_LINES,coordinateLinesStr);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  it->first << "\">" <<  it->first << "</OPTION>\n";
        }

        a++;
      }
      ostr1 << "</SELECT>\n";
      ostr1 << "</TD></TR>\n";


      // ### Land border:

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Land border:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LAND_BORDER << "=' + this.options[this.selectedIndex].value)\">\n";

      for (auto it = itsColors.begin(); it != itsColors.end(); ++it)
      {
        if (landBorderStr == it->first)
        {
          ostr1 << "<OPTION selected value=\"" << it->first << "\">" <<  it->first << "</OPTION>\n";
          session.setAttribute(ATTR_LAND_BORDER,landBorderStr);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  it->first << "\">" <<  it->first << "</OPTION>\n";
        }
        a++;
      }
      ostr1 << "</SELECT>\n";
      ostr1 << "</TD></TR>\n";


      // ### Land and sea masks:

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Land and sea colors:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LAND_MASK << "=' + this.options[this.selectedIndex].value)\">\n";

      a = 0;
      for (auto it = itsColors.begin(); it != itsColors.end(); ++it)
      {
        if (landMaskStr == it->first)
        {
          ostr1 << "<OPTION selected value=\"" << it->first << "\">" <<  it->first << "</OPTION>\n";
          session.setAttribute(ATTR_LAND_MASK,landMaskStr);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  it->first << "\">" <<  it->first << "</OPTION>\n";
        }

        a++;
      }
      ostr1 << "</SELECT>\n";


      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SEA_MASK << "=' + this.options[this.selectedIndex].value)\">\n";

      a = 0;
      for (auto it = itsColors.begin(); it != itsColors.end(); ++it)
      {
        if (seaMaskStr == it->first)
        {
          ostr1 << "<OPTION selected value=\"" << it->first << "\">" <<  it->first << "</OPTION>\n";
          session.setAttribute(ATTR_SEA_MASK,seaMaskStr);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  it->first << "\">" <<  it->first << "</OPTION>\n";
        }

        a++;
      }

      ostr1 << "</SELECT>\n";
      ostr1 << "</TD></TR>\n";


      // ### Missing value:

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Missing Value:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:250px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_MISSING << "=' + this.options[this.selectedIndex].value)\">\n";

      const char *missingValues[] = {"Default","Zero",nullptr};

      a = 0 ;
      while (missingValues[a] != nullptr)
      {
        if (missingStr == missingValues[a])
        {
          ostr1 << "<OPTION selected value=\"" << missingValues[a] << "\">" <<  missingValues[a] << "</OPTION>\n";
          session.setAttribute(ATTR_MISSING,missingStr);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  missingValues[a] << "\">" <<  missingValues[a] << "</OPTION>\n";
        }
        a++;
      }
      ostr1 << "</SELECT>\n";
      ostr1 << "</TD></TR>\n";



      // ## FMI key:

      ostr1 << "<TR height=\"15\" style=\"font-size:12; width:250px;\"><TD>FMI Key:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD><INPUT type=\"text\" value=\"" << fmiKeyStr << "\"></TD></TR>\n";

      // ### Units:

      ostr1 << "<TR height=\"15\" style=\"font-size:12; width:250px;\"><TD>Units:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD><INPUT type=\"text\" value=\"" << unitStr << "\"></TD></TR>\n";


      // ## Value:

      ostr1 << "<TR height=\"15\" style=\"font-size:12; width:250px;\"><TD>Value:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD><INPUT type=\"text\" id=\"gridValue\"></TD></TR>\n";

    }

    ostr1 << "<TR height=\"50%\"><TD></TD></TR>\n";

    // ## Download
    ostr1 << "<TR height=\"30\" style=\"font-size:16; font-weight:bold; width:250px; color:#000000; background:#D0D0D0; vertical-align:middle; text-align:center; \"><TD><a href=\"grid-gui?" << ATTR_PAGE << "=download&" << ATTR_FILE_ID << "=" << fileIdStr << "&" << ATTR_MESSAGE_INDEX << "=" << messageIndexStr << "\">Download</a></TD></TR>\n";
    ostr1 << "</TABLE>\n";

    ostr2 << "<TABLE width=\"100%\" height=\"100%\">\n";

    if (itsAnimationEnabled  &&  (presentation == "Image" || presentation == "Map" || presentation == "Symbols" ||  presentation == "Isolines"  || /* presentation == "Dali" ||*/ presentation == "Streams"))
      ostr2 << "<TR><TD style=\"height:35; width:100%; vertical-align:middle; text-align:left; font-size:12;\">" << ostr3.str() << "</TD></TR>\n";

    if (presentation == "Image")
    {
      ostr2 << "<TR><TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:1000;\" src=\"/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\" onclick=\"getImageCoords(event,this," << fileIdStr << "," << messageIndexStr << ",'" << presentation << "');\"/></TD></TR>";
    }
    else
    if (presentation == "Symbols")
    {
      ostr2 << "<TR><TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:100%;\" src=\"/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\" onclick=\"getImageCoords(event,this," << fileIdStr << "," << messageIndexStr << ",'" << presentation << "');\"/></TD></TR>";
    }
    else
    if (presentation == "Isolines")
    {
      ostr2 << "<TR><TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:1000;\" src=\"/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\" onclick=\"getImageCoords(event,this," << fileIdStr << "," << messageIndexStr << ",'" << presentation << "');\"/></TD></TR>";
    }
    else
    if (presentation == "Map")
    {
      ostr2 << "<TR><TD><IMG id=\"myimage\" style=\"background:#000000; max-width:100%; height:100%;\" src=\"/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\"/></TD></TR>";
    }
    else
    if (presentation == "Table(sample)" /* || presentation == "table(full)"*/)
    {
      ostr2 << "<TR><TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=table\">";
      ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ostr2 << "</IFRAME></TD></TR>";
    }
    else
    if (presentation == "Coordinates(sample)" /* || presentation == "coordinates(full)"*/)
    {
      ostr2 << "<TR><TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=coordinates\">";
      ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ostr2 << "</IFRAME></TD></TR>";
    }
    else
    if (presentation == "Info")
    {
      ostr2 << "<TR><TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\">";
      ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ostr2 << "</IFRAME></TD></TR>";
    }
    else
    if (presentation == "Streams" || presentation == "StreamsAnimation")
    {
      ostr2 << "<TR><TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:1000;\" src=\"/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\" onclick=\"getImageCoords(event,this," << fileIdStr << "," << messageIndexStr << ",'" << presentation << "');\"/></TD></TR>";
    }
    else
    if (presentation == "Message")
    {
      ostr2 << "<TR><TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\">";
      ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ostr2 << "</IFRAME></TD></TR>";
    }
    else
    if (presentation == "Locations")
    {
      ostr2 << "<TR><TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\">";
      ostr2 << "<p>Your browser does not support iframes.</p>\n";
      ostr2 << "</IFRAME></TD></TR>";
    }
    /*
    else
    if (presentation == "Dali")
    {
      ostr2 << "<TR><TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:1000;\" src=\"" + daliUrl << "&start=" << timeStr << "\" /></TD></TR>";
    }
    */

    ostr2 << "<TR><TD style=\"height:25; vertical-align:middle; text-align:left; font-size:12;\">" << paramDescription << "</TD></TR>\n";

    ostr2 << "</TABLE>\n";


    output << "<TABLE width=\"100%\" height=\"100%\">\n";
    output << "<TR>\n";

    output << "<TD  bgcolor=\"#C0C0C0\" width=\"180\">\n";
    output << ostr1.str();
    output << "</TD>\n";

    output << "<TD>\n";
    output << ostr2.str();
    output << "</TD>\n";

    output << "</TR>\n";
    output << "</TABLE>\n";
    output << "</BODY></HTML>\n";

    theResponse.setContent(std::string(output.str()));
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





int Plugin::request(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  FUNCTION_TRACE
  try
  {
    int result = HTTP::Status::ok;
    int expires_seconds = 1;

    Session session;

    boost::optional<std::string> v;
    v = theRequest.getParameter("session");
    if (v)
    {
      //printf("**** SESSION FOUND\n");
      session.setAttributes(*v);
    }
    else
      initSession(session);

    Spine::HTTP::ParamMap map = theRequest.getParameterMap();
    for (auto it = map.begin(); it != map.end(); it++)
    {
      //printf("SEARCH [%s][%s]\n",it->first.c_str(),it->second.c_str());
      std::string value;
      if (strcasecmp(it->first.c_str(),"session") != 0  &&  session.getAttribute(it->first.c_str(),value))
      {
        if (value != it->second)
        {
          session.setAttribute(it->first.c_str(),it->second.c_str());
          std::string name = std::string("#") + it->first;
          session.setAttribute(name.c_str(),value.c_str());
        }
      }
    }


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

    if (strcasecmp(page.c_str(),"main") == 0)
    {
      result = page_main(theReactor,theRequest,theResponse,session);
    }
    else
    if (strcasecmp(page.c_str(),"image") == 0)
    {
      result = page_image(theReactor,theRequest,theResponse,session);
      expires_seconds = 600;
    }
    else
    if (strcasecmp(page.c_str(),"symbols") == 0)
    {
      result = page_symbols(theReactor,theRequest,theResponse,session);
      expires_seconds = 600;
    }
    else
    if (strcasecmp(page.c_str(),"isolines") == 0)
    {
      result = page_isolines(theReactor,theRequest,theResponse,session);
      expires_seconds = 600;
    }
    else
    if (strcasecmp(page.c_str(),"streams") == 0)
    {
      result = page_streams(theReactor,theRequest,theResponse,session);
      expires_seconds = 600;
    }
    else
    if (strcasecmp(page.c_str(),"streamsAnimation") == 0)
    {
      result = page_streamsAnimation(theReactor,theRequest,theResponse,session);
      expires_seconds = 600;
    }
    else
    if (strcasecmp(page.c_str(),"map") == 0)
    {
      result = page_map(theReactor,theRequest,theResponse,session);
      expires_seconds = 600;
    }
    else
    if (strcasecmp(page.c_str(),"info") == 0)
    {
      result = page_info(theReactor,theRequest,theResponse,session);
    }
    else
    if (strcasecmp(page.c_str(),"message") == 0)
    {
      result = page_message(theReactor,theRequest,theResponse,session);
    }
    else
    if (strcasecmp(page.c_str(),"download") == 0)
    {
      result = page_download(theReactor,theRequest,theResponse,session);
    }
    else
    if (strcasecmp(page.c_str(),"locations") == 0)
    {
      result = page_locations(theReactor,theRequest,theResponse,session);
    }
    else
    if (strcasecmp(page.c_str(),"table") == 0)
    {
      result = page_table(theReactor,theRequest,theResponse,session);
      expires_seconds = 600;
    }
    else
    if (strcasecmp(page.c_str(),"coordinates") == 0)
    {
      result = page_coordinates(theReactor,theRequest,theResponse,session);
      expires_seconds = 600;
    }
    else
    if (strcasecmp(page.c_str(),"value") == 0)
    {
      result = page_value(theReactor,theRequest,theResponse,session);
    }
    else
    if (strcasecmp(page.c_str(),"timeseries") == 0)
    {
      result = page_timeseries(theReactor,theRequest,theResponse,session);
    }

    boost::posix_time::ptime t_now = boost::posix_time::second_clock::universal_time();
    boost::posix_time::ptime t_expires = t_now + boost::posix_time::seconds(expires_seconds);
    boost::shared_ptr<Fmi::TimeFormatter> tformat(Fmi::TimeFormatter::create("http"));
    std::string cachecontrol = "public, max-age=" + boost::lexical_cast<std::string>(expires_seconds);
    std::string expiration = tformat->format(t_expires);
    std::string modification = tformat->format(t_now);

    theResponse.setHeader("Cache-Control", cachecontrol.c_str());
    theResponse.setHeader("Expires", expiration.c_str());

    if (result == HTTP::Status::ok)
      theResponse.setHeader("Last-Modified", modification.c_str());

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

void Plugin::requestHandler(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  FUNCTION_TRACE
  try
  {
    try
    {
      // Check request method (support GET, OPTIONS)
      if (checkRequest(theRequest, theResponse, false)) {
        return;
      }

      //auto headers = theRequest.getHeaders();
      //for (auto it = headers.begin(); it != headers.end(); ++it)
      //  std::cout << it->first << " = " << it->second << "\n";

      theResponse.setHeader("Access-Control-Allow-Origin", "*");
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
      theResponse.setHeader("X-Content-Error", firstMessage.c_str());

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

int Plugin::getRequiredAPIVersion() const
{
  return SMARTMET_API_VERSION;
}




// ----------------------------------------------------------------------
/*!
 * \brief Performance query implementation.
 *
 * We want admin calls to always be processed ASAP
 */
// ----------------------------------------------------------------------

bool Plugin::queryIsFast(const Spine::HTTP::Request & /* theRequest */) const
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
