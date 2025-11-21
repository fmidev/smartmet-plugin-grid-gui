// ======================================================================
/*!
 * \brief SmartMet Grid Gui plugin implementation
 */
// ======================================================================

#include "Plugin.h"

#include <grid-files/common/GeneralFunctions.h>
#include <grid-files/common/ImagePaint.h>
#include <grid-files/identification/GridDef.h>
#include <grid-files/map/Topography.h>
#include <grid-files/common/ShowFunction.h>
#include <spine/SmartMet.h>
#include <macgyver/Hash.h>
#include <macgyver/TimeFormatter.h>
#include <macgyver/FastMath.h>
#include <boost/bind/bind.hpp>
#include <macgyver/DateTime.h>
#include <boost/lexical_cast.hpp>
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

#define ATTR_STREAM_COLOR       "sc"
#define ATTR_BLUR               "bl"
#define ATTR_COLOR_MAP          "cm"
#define ATTR_OPACITY            "op"
#define ATTR_COORDINATE_LINES   "cl"
#define ATTR_FILE_ID            "f"
#define ATTR_FMI_KEY            "k"
#define ATTR_FORECAST_NUMBER    "fn"
#define ATTR_FORECAST_TYPE      "ft"
#define ATTR_GENERATION_ID      "g"
#define ATTR_GEOMETRY_ID        "gm"
#define ATTR_HUE                "hu"
#define ATTR_LAND_BORDER        "lb"
#define ATTR_LAND_MASK          "lm"
#define ATTR_LEVEL              "l"
#define ATTR_LEVEL_ID           "lt"
#define ATTR_MAX_LENGTH         "max"
#define ATTR_MESSAGE_INDEX      "m"
#define ATTR_MIN_LENGTH         "min"
#define ATTR_MISSING            "mi"
#define ATTR_PAGE               "pg"
#define ATTR_PARAMETER_ID       "p"
#define ATTR_PRESENTATION       "pre"
#define ATTR_PRODUCER_ID        "pi"
#define ATTR_PRODUCER_NAME      "pn"
#define ATTR_PROJECTION_ID      "pro"
#define ATTR_SATURATION         "sa"
#define ATTR_SEA_MASK           "sm"
#define ATTR_STEP               "st"
#define ATTR_TIME               "t"
#define ATTR_UNIT               "u"
#define ATTR_X                  "xx"
#define ATTR_Y                  "yy"
#define ATTR_TIME_GROUP_TYPE    "tgt"
#define ATTR_TIME_GROUP         "tg"
#define ATTR_PROJECTION_LOCK    "pl"

#define ATTR_LAND_SHADING_LIGHT  "lsl"
#define ATTR_LAND_SHADING_SHADOW "lss"
#define ATTR_LAND_SHADING_POS    "lsp"
#define ATTR_LAND_COLOR_POS      "lcp"

#define ATTR_SEA_SHADING_LIGHT   "ssl"
#define ATTR_SEA_SHADING_SHADOW  "sss"
#define ATTR_SEA_SHADING_POS     "ssp"
#define ATTR_SEA_COLOR_POS       "scp"


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
    for (auto it=projVec.begin(); it != projVec.end(); ++it)
      itsBlockedProjections.insert(std::stoi(*it));

    Identification::gridDef.init(itsGridConfigFile.c_str());
    Map::topography.init(itsGridConfigFile.c_str(),true,true,true);

    for (auto it = itsColorMapFileNames.begin(); it != itsColorMapFileNames.end(); ++it)
    {
      T::ColorMapFile colorMapFile;
      colorMapFile.init(it->c_str());
      itsColorMapFiles.emplace_back(colorMapFile);
    }

    loadColorFile();
    loadProducerFile();


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

    while (!feof(file))
    {
      if (fgets(st, 1000, file) != nullptr && st[0] != '#')
      {
        bool ind = false;
        char* field[100];
        uint c = 1;
        field[0] = st;
        char* p = st;
        while (*p != '\0' && c < 100)
        {
          if (*p == '"')
            ind = !ind;

          if ((*p == ';' || *p == '\n') && !ind)
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
    fclose(file);

    itsProducerFile_modificationTime = getFileModificationTime(itsProducerFile.c_str());
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
            uint color = 0x00FFFFFF;
            if (strlen(field[1]) == 8)
              color = strtoul(field[1],nullptr,16);
            else  // No alpha
              color = 0xFF000000 + strtoul(field[1],nullptr,16);

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

    return 0x00FFFFFF;
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
    double step = 0;

    if (!colorMapFile)
    {
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
      double avg = total / (double)cnt;
      double dd = maxValue - minValue;
      double ddd = avg-minValue;
      step = dd / 200;
      if (maxValue > (minValue + 5*ddd))
        step = 5*ddd / 200;
    }

    int width = columns;
    int height = rows;

    uint xx = columns / 36;
    uint yy = rows / 18;

    double dWidth = C_DOUBLE(width);
    double dHeight = C_DOUBLE(height);

    double xd = 360/dWidth;
    double yd = 180/dHeight;


    uint *image = new uint[width*height];

    uint c = 0;
    bool yLand[width];
    for (int x=0; x<width; x++)
      yLand[x] = false;

    ModificationLock *modificationLock = NULL;
    if (colorMapFile != nullptr)
      modificationLock = colorMapFile->getModificationLock();

    uint lbcol = landBorder;

    AutoReadLock lock(modificationLock);

    for (int y=0; y<height; y++)
    {
      bool prevLand = false;
      for (int x=0; x<width; x++)
      {
        T::ParamValue val = values[c];
        if (val == 0.0   &&  zeroIsMissingValue)
          val = ParamValueMissing;

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

        double xc = xd*(x-(dWidth/2));
        double yc = yd*((dHeight-y-1)-(dHeight/2));

        bool land = Map::topography.isLand(xc,yc);

        if (land  &&  (val == ParamValueMissing || ((col & 0xFF000000) == 0)))
          col = landColor;

        if (!land &&  (val == ParamValueMissing || ((col & 0xFF000000) == 0)))
          col = seaColor;

        if (landBorder & 0xFF000000)
        {
          if (land & (!prevLand || !yLand[x]))
          {
            col = landBorder;
            lbcol = col;
          }

          if (!land)
          {
            if (prevLand  &&  x > 0  &&  image[y*width + x-1] != coordinateLines)
              image[y*width + x-1] = lbcol;

            if (yLand[x] &&  y > 0  && image[(y-1)*width + x] != coordinateLines)
              image[(y-1)*width + x] = lbcol;
          }
        }

        if ((coordinateLines & 0xFF000000) && ((x % xx) == 0  ||  (y % yy) == 0))
        {
          col = coordinateLines;
        }

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





void Plugin::saveImage(ImagePaintParameters& params)
{
  FUNCTION_TRACE
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

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

    if (geomId == params.geometryId)
    {
      T::GridData gridData;
      int result = dataServer->getGridData(0,params.fileId,params.messageIndex,gridData);
      if (result != 0)
      {
        Fmi::Exception exception(BCP,"Data fetching failed!");
        exception.addParameter("Result",DataServer::getResultString(result));
        throw exception;
      }

      saveImage(params,gridData.mColumns,gridData.mRows,gridData.mValues,*coordinates,lineCoordinates);
      //saveImage(imageFile,gridData.mColumns,gridData.mRows,gridData.mValues,*coordinates,*lineCoordinates,hue,saturation,blur,coordinateLines,landBorder,landMask,seaMask,colorMapName,missingStr,geometryId,pstep,minLength,maxLength,lightBackground,animation);
    }
    else
    {
      params.geometryId = geomId;
      uint cols = 0;
      uint rows = 0;
      if (Identification::gridDef.getGridDimensionsByGeometryId(geomId,cols,rows))
      {
        T::ParamValue_vec values;

        short interpolationMethod = T::AreaInterpolationMethod::Linear;

        T::AttributeList attributeList;
        attributeList.addAttribute("grid.geometryId",std::to_string(geomId));
        attributeList.addAttribute("grid.areaInterpolationMethod",std::to_string(interpolationMethod));
        if (params.fileId > 0)
        {
          double_vec modificationParameters;
          int result = dataServer->getGridValueVectorByGeometry(0,params.fileId,params.messageIndex,attributeList,0,modificationParameters,values);
          if (result != 0)
            throw Fmi::Exception(BCP,"Data fetching failed!");

          saveImage(params,cols,rows,values,*coordinates,lineCoordinates);
          //saveImage(imageFile,cols,rows,values,*coordinates,*lineCoordinates,hue,saturation,blur,coordinateLines,landBorder,landMask,seaMask,colorMapName,missingStr,geomId,pstep,minLength,maxLength,lightBackground,animation);
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

    uint size = width*height;
    std::size_t sz = values.size();

    if (size == 0 || size > 100000000)
      return;

    if (sz < size)
    {
      printf("ERROR: There are not enough values (= %lu) for the grid (%u x %u)!\n",sz,width,height);
      return;
    }

    bool rotate = true;
    if (coordinates.size() > C_UINT(10*width)  &&  coordinates[0].y() < coordinates[10*width].y())
      rotate = true;
    else
      rotate = false;


    uint *finalImage = new uint[size];
    uint *valImage = nullptr;
    uint *landShadingImage = nullptr;
    uint *seaShadingImage = nullptr;
    uint *landImage = nullptr;
    uint *landBorderImage = nullptr;
    uint *seaImage = nullptr;

    if (size == coordinates.size() && (params.seaShading_light || params.seaShading_shadow))
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
              double m = Map::topography.getSeaShading(lon,lat);
              if (m < 0)
              {
                uint pp = (uint)(-(double)params.seaShading_shadow*m);
                if (pp > 255)
                  pp = 255;

                seaShadingImage[pixpos] = pp << 24;
              }
              else
              {
                uint pp = (uint)((double)params.seaShading_light*m);
                if (pp > 255)
                  pp = 255;

                seaShadingImage[pixpos] = (pp << 24) + 0xFFFFFF;
              }
            }
          }
          c++;
        }
      }
    }


    if (size == coordinates.size() && (params.landShading_light || params.landShading_shadow || (params.landBorder_color & 0xFF000000)))
    {
      // Counting land topography.

      bool yLand[width];
      for (int x=0; x<width; x++)
        yLand[x] = false;

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
;
            if (params.landShading_position)
            {
              double m = (double)Map::topography.getLandShading(lon,lat);
              if (m < 0)
              {
                uint pp = (uint)(-(double)params.landShading_shadow*m);
                if (pp > 255)
                  pp = 255;

                landShadingImage[pixpos] = pp << 24;
              }
              else
              {
                uint pp = (uint)((double)params.landShading_light*m);
                if (pp > 255)
                  pp = 255;

                landShadingImage[pixpos] = (pp << 24) + 0xFFFFFF;
              }
            }
          }
          else
          {
            if (prevLand  &&  x > 0)
            {
              int ppos = yy*width + (x-1);
              landBorderImage[ppos] = params.landBorder_color;
            }

            if (yLand[x] &&  yy > 0)
            {
              int ppos = (yy-1)*width + x-1;
              landBorderImage[ppos] = params.landBorder_color;
            }
          }
          yLand[x] = land;
          prevLand = land;
          c++;
        }
      }
    }

    uint alpha = (uint)params.paint_alpha << 24;

    if (!colorMapFile && params.stream_step == 0)
    {
      // We do not have colormap - using HSV instead

      valImage = new uint[size];

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

      double avg = total / (double)cnt;
      double dd = maxValue - minValue;
      double ddd = avg-minValue;
      double step = dd / 200;
      if (maxValue > (minValue + 5*ddd))
        step = 5*ddd / 200;

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
    }


    T::ByteData_vec contours;

    if (colorMapFile && params.stream_step == 0)
    {
      valImage = new uint[size];

      double amp = (double)params.paint_alpha/255.0;


      uint c = 0;
      ModificationLock *modificationLock = colorMapFile->getModificationLock();
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

          uint col = colorMapFile->getSmoothColor(val);
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
    }

    for (uint t=0; t<size; t++)
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

      finalImage[t] = col;
    }


    if ((params.coordinateLine_color & 0xFF000000)  &&  lineCoordinates  &&  lineCoordinates->size() > 0)
    {
      ImagePaint imagePaint(finalImage,width,height,0x0,params.coordinateLine_color,0xFFFFFFFF,false,rotate);

      for (auto it = lineCoordinates->begin(); it != lineCoordinates->end(); ++it)
        imagePaint.paintPixel(C_INT(Fmi::floor(it->x())),C_INT(Fmi::floor(it->y())),params.coordinateLine_color);
    }


    if (landImage)
      delete [] landImage;

    if (seaImage)
      delete [] seaImage;

    if (landShadingImage)
      delete [] landShadingImage;

    if (seaShadingImage)
      delete [] seaShadingImage;

    if (valImage)
      delete [] valImage;

    if (landBorderImage)
      delete [] landBorderImage;


    if (params.stream_step > 0)
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

      //uint *img = finalImage; //imagePaint.getImage();
      uint *streamImage = new uint[size];

      getStreamlineImage(direction,nullptr,streamImage,width,height,params.stream_step,params.stream_step,params.stream_minLength,params.stream_maxLength);

      uint lcolmax = 0xD0;
      uint lcolors = 16;
      uint color[lcolors];

      uint sc = params.stream_color & 0x00FFFFFF;

      double mp = (double)lcolmax / (double)lcolors;

      for (uint t=0; t<lcolors; t++)
      {
        uint cc = lcolmax - (uint)(t*mp);
        color[t] = (cc << 24) + sc;
      }

      if (params.stream_animation)
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

        webp_anim_save(params.imageFile.c_str(),wimage,width,height,lcolors,50);

        for (uint t=0; t<lcolors; t++)
          delete [] wimage[t];

        delete [] direction;
        delete [] streamImage;
        return;
      }
      else
      {
        for (uint t=0; t<size; t++)
        {
          uint streamCol = streamImage[t];
          if (streamCol != 0)
          {
            finalImage[t] = merge_ARGB(color[(streamCol-1) % lcolors],finalImage[t]);
          }
        }
      }
      delete [] direction;
      delete [] streamImage;
    }


    if (finalImage)
    {
      //jpeg_save(params.imageFile.c_str(),finalImage,height,width,100);
      png_save(params.imageFile.c_str(),finalImage,width,height,1);

      delete [] finalImage;
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


    T::ParamValue max = ParamValueMissing;
    uint vcnt = gridData.mValues.size();
    for (uint t=0; t<vcnt; t++)
    {
      if (gridData.mValues[t] != ParamValueMissing  &&  (max == ParamValueMissing ||  gridData.mValues[t] > max))
        max = gridData.mValues[t];
    }

    std::string formatStr;
    if (max < 0.00001)
      formatStr = "%.14f";
    else
    if (max < 0.001)
      formatStr = "%.12f";
    else
    if (max < 0.1)
      formatStr = "%.6f";
    else
      formatStr = "%.3f";

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
        {
          if (gridData.mValues[c] != ParamValueMissing)
          {
            sprintf(tmp,formatStr.c_str(),gridData.mValues[c]);
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

      std::shared_ptr<std::vector<char>> sContent;
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

        //theResponse.setHeader("Content-Type","image/jpg");
        theResponse.setHeader("Content-Type","image/png");
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
      landMaskStr + ":" + seaMaskStr + ":" + colorMapFileName + ":" + colorMapModificationTime + ":" + missingStr + ":" +
      landShadingLightStr + ":" + landShadingShadowStr  + ":" + landShadingPositionStr  + ":" + landColorPosStr + ":" +
      seaShadingLightStr + ":" + seaShadingShadowStr  + ":" + seaShadingPositionStr  + ":" + seaColorPosStr + ":" + opacityStr + ":" + streamColorStr;

    const std::size_t seed = Fmi::hash(hash);
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
      char fname[200];
      sprintf(fname,"%s/grid-gui-image_%llu.png",itsImageCache_dir.c_str(),getTime());

      ImagePaintParameters params;

      params.imageFile = fname;
      params.fileId = toUInt32(fileIdStr);
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

      saveImage(params);

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

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    std::string hash = "Streams:" + fileIdStr + ":" + messageIndexStr + ":" + hueStr + ":" + saturationStr + ":" +
      blurStr + ":" + coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + colorMapFileName + ":" + colorMapModificationTime + ":" + missingStr + ":" +
      minLengthStr + ":" + maxLengthStr + ":" + stepStr + ":" + streamColorStr + ":" +
      landShadingLightStr + ":" + landShadingShadowStr  + ":" + landShadingPositionStr  + ":" + landColorPosStr + ":" +
      seaShadingLightStr + ":" + seaShadingShadowStr  + ":" + seaShadingPositionStr  + ":" + seaColorPosStr + ":" + opacityStr;

    const std::size_t seed = Fmi::hash(hash);
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
      char fname[200];
      sprintf(fname,"%s/grid-gui-image_%llu.png",itsImageCache_dir.c_str(),getTime());

      ImagePaintParameters params;

      params.imageFile = fname;
      params.fileId = toUInt32(fileIdStr);
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

      saveImage(params);

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

    if (projectionIdStr.empty())
      projectionIdStr = geometryIdStr;

    std::string hash = "StreamsAnimation:" + fileIdStr + ":" + messageIndexStr + ":" + hueStr + ":" + saturationStr + ":" +
      blurStr + ":" + coordinateLinesStr + ":" + landBorderStr + ":" + projectionIdStr + ":" +
      landMaskStr + ":" + seaMaskStr + ":" + colorMapFileName + ":" + colorMapModificationTime + ":" + missingStr + ":" +
      minLengthStr + ":" + maxLengthStr + ":" + stepStr + ":" + streamColorStr + ":" +
      landShadingLightStr + ":" + landShadingShadowStr  + ":" + landShadingPositionStr  + ":" + landColorPosStr + ":" +
      seaShadingLightStr + ":" + seaShadingShadowStr  + ":" + seaShadingPositionStr  + ":" + seaColorPosStr + ":" + opacityStr;

    const std::size_t seed = Fmi::hash(hash);
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
      char fname[200];
      sprintf(fname,"%s/grid-gui-image_%llu.webp",itsImageCache_dir.c_str(),getTime());

      ImagePaintParameters params;

      params.imageFile = fname;
      params.fileId = toUInt32(fileIdStr);
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
      params.stream_animation = true;
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

    const std::size_t seed = Fmi::hash(hash);
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
      sprintf(fname,"/%s/grid-gui-image_%llu.png",itsImageCache_dir.c_str(),getTime());
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




int Plugin::page_main(Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse,
                            Session& session)
{
  FUNCTION_TRACE
  try
  {
    const char *layerPosition[] = {"None","Bottom","Top",NULL};

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
    std::string timeGroupTypeStr = session.getAttribute(ATTR_TIME_GROUP_TYPE);
    std::string timeGroupStr = session.getAttribute(ATTR_TIME_GROUP);
    std::string projectionLockStr = session.getAttribute(ATTR_PROJECTION_LOCK);
    std::string landShadingLightStr = session.getAttribute(ATTR_LAND_SHADING_LIGHT);
    std::string landShadingShadowStr = session.getAttribute(ATTR_LAND_SHADING_SHADOW);
    std::string landShadingPositionStr = session.getAttribute(ATTR_LAND_SHADING_POS);
    std::string landColorPosStr = session.getAttribute(ATTR_LAND_COLOR_POS);
    std::string seaShadingLightStr = session.getAttribute(ATTR_SEA_SHADING_LIGHT);
    std::string seaShadingShadowStr = session.getAttribute(ATTR_SEA_SHADING_SHADOW);
    std::string seaShadingPositionStr = session.getAttribute(ATTR_SEA_SHADING_POS);
    std::string seaColorPosStr = session.getAttribute(ATTR_SEA_COLOR_POS);

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
      timeGroupStr = "";
      session.setAttribute(ATTR_TIME_GROUP,"");
      timeStr = "";
      session.setAttribute(ATTR_TIME,"");
      if (projectionLockStr.empty())
      {
        projectionIdStr = "";
        session.setAttribute(ATTR_PROJECTION_ID,"");
      }
      fileIdStr = "";
      session.setAttribute(ATTR_FILE_ID,"");
      messageIndexStr = "0";
      session.setAttribute(ATTR_MESSAGE_INDEX,"0");
    }

    if (timeGroupTypeStr.empty() || session.findAttribute("#",ATTR_TIME_GROUP_TYPE))
    {
      timeGroupStr = "";
      session.setAttribute(ATTR_TIME_GROUP,"");
      timeStr = "";
      session.setAttribute(ATTR_TIME,"");
    }

    time_t requiredAccessTime = time(nullptr) + 120;

    if (getFileModificationTime(itsColorFile.c_str()) != itsColors_lastModified)
    {
      loadColorFile();
    }

    loadProducerFile();

    uint landshade_light = toUInt32(landShadingLightStr);
    uint landshade_shadow = toUInt32(landShadingShadowStr);
    uint landshade_pos = toUInt32(landShadingPositionStr);
    uint landcol_pos = toUInt32(landColorPosStr);

    uint seashade_light = toUInt32(seaShadingLightStr);
    uint seashade_shadow = toUInt32(seaShadingShadowStr);
    uint seashade_pos = toUInt32(seaShadingPositionStr);
    uint seacol_pos = toUInt32(seaColorPosStr);
    uint opacity = toUInt32(opacityStr);


    std::ostringstream output;
    std::ostringstream ostr1;
    std::ostringstream ostr2;
    std::ostringstream ostr3;
    std::ostringstream ostr4;

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
    output << "  var url = \"/grid-gui?session=" << ATTR_PAGE << "=value;" << ATTR_PRESENTATION << "=\" + presentation + \";" << ATTR_FILE_ID << "=\" + fileId + \";" << ATTR_MESSAGE_INDEX << "=\" + messageIndex + \";" << ATTR_X << "=\" + prosX + \";" << ATTR_Y << "=\" + prosY;\n";
    output << "  var txt = httpGet(url);\n";
    output << "  document.getElementById('gridValue').value = txt;\n";

    output << "}\n";
    output << "</SCRIPT>\n";


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
      ostr1 << "<SELECT style=\"width:280px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PRODUCER_ID << "=' + this.options[this.selectedIndex].value)\">\n";
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
    bool generationNotReady = false;

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

      ostr1 << "<SELECT style=\"width:280px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_GENERATION_ID << "=' + this.options[this.selectedIndex].value)\">\n";

      for (auto it = generations.rbegin(); it != generations.rend(); ++it)
      {
        std::string name = *it;
        T::GenerationInfo *g = generationInfoList.getGenerationInfoByName(name);
        if (g != nullptr && (g->mDeletionTime == 0 || g->mDeletionTime > requiredAccessTime))
        {
          std::string status = "";
          if (g->mStatus != 1)
            status = " (* not ready *)";

          if (generationId == 0)
          {
            generationId = g->mGenerationId;
            generationIdStr = std::to_string(generationId);
            //printf("EMPTY => GENERATION %s\n",generationIdStr.c_str());
          }

          if (generationId == g->mGenerationId)
          {
            if (g->mStatus != 1)
              generationNotReady = true;

            originTime = g->mAnalysisTime;
            ostr1 << "<OPTION selected value=\"" <<  g->mGenerationId << "\">" <<  g->mName << status << "</OPTION>\n";
            session.setAttribute(ATTR_GENERATION_ID,generationId);
          }
          else
            ostr1 << "<OPTION value=\"" <<  g->mGenerationId << "\">" << g->mName << status << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";
    }
    ostr1 << "</TD></TR>\n";

    if (generationNotReady)
    {
      ostr1 << "<TR style=\"text-align:center; font-size:12; font-weight:bold;\"><TD>*** Generation not ready ***</TD></TR>\n";
    }


    // ### Parameters:

    std::string paramDescription;
    std::set<std::string> paramKeyList;
    contentServer->getContentParamKeyListByGenerationId(0,generationId,T::ParamKeyTypeValue::FMI_NAME,paramKeyList);

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Parameter:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";

    if (paramKeyList.size() > 0)
    {
      ostr1 << "<SELECT style=\"width:280px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PARAMETER_ID << "=' + this.options[this.selectedIndex].value)\">\n";
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

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Level type and value:</TD></TR>\n";
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

      ostr1 << "<SELECT style=\"width:200px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LEVEL_ID << "=' + this.options[this.selectedIndex].value) \">\n";
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

    // ### Levels:

    contentInfoList.clear();
    contentServer->getContentListByParameterAndGenerationId(0,generationId,T::ParamKeyTypeValue::FMI_NAME,parameterIdStr,levelId,0,0x7FFFFFFF,-2,-2,-2,"14000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    T::ParamLevel level = toInt32(levelStr);

    std::set<int> levels;
    getLevels(contentInfoList,levelId,levels);

    if (levels.find(level) == levels.end())
      level = 0;

    if (levels.size() > 0)
    {
      std::string disabled = "";
      if (levels.size() == 1)
        disabled = "disabled";

      ostr1 << "<SELECT style=\"width:70px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LEVEL << "=' + this.options[this.selectedIndex].value)\">\n";
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

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Forecast type and number:</TD></TR>\n";
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

      ostr1 << "<SELECT style=\"width:200px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_FORECAST_TYPE << "=' + this.options[this.selectedIndex].value)\">\n";
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

    // ### Forecast number:

    short forecastNumber = toInt16(forecastNumberStr);

    std::set<int> forecastNumbers;
    getForecastNumbers(contentInfoList,levelId,level,forecastType,forecastNumbers);

    if (forecastNumbers.find(forecastNumber) == forecastNumbers.end())
      forecastNumber = 0;

    if (forecastNumbers.size() > 0)
    {
      std::string disabled = "";
      if (forecastNumbers.size() == 1)
        disabled = "disabled";

      ostr1 << "<SELECT style=\"width:70px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_FORECAST_NUMBER << "=' + this.options[this.selectedIndex].value)\">\n";
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

      ostr1 << "<SELECT style=\"width:280px;\" " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_GEOMETRY_ID << "=' + this.options[this.selectedIndex].value)\">\n";

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


    if (geometryId != 0)
    {
      char tmp[100];
      sprintf(tmp,"%s:%u",producerNameStr.c_str(),geometryId);
      if (itsProducerList.find(toUpperString(tmp)) == itsProducerList.end())
      {
        ostr1 << "<TR style=\"text-align:center; font-size:12; font-weight:bold;\"><TD >*** Search not configured ***</TD></TR>\n";
      }
    }


    if (projectionIdStr.empty())
    {
      projectionIdStr = geometryIdStr;
      session.setAttribute(ATTR_PROJECTION_ID,projectionIdStr);
    }

    // ### Times:

    contentInfoList.clear();
    contentServer->getContentListByParameterAndGenerationId(0,generationId,T::ParamKeyTypeValue::FMI_NAME,parameterIdStr,levelId,level,level,-2,-2,-2,"14000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();

    std::string pTime = timeStr;
    std::set<std::string> timeGroupStrList;

    uint timeGroupType = 0;
    bool useTimeGroup = false;
    const char *timeGroupTypes[] = {"All","Day","Month","Year",nullptr};
    int timeGroupLen[] = {15,8,6,4};


    ostr1 << "<TR height=\"15\"><TD><HR/></TD></TR>\n";
    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Time group:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD><TABLE><TR><TD>\n";

    ostr1 << "<SELECT style=\"width:80px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_TIME_GROUP << "=&" << ATTR_TIME_GROUP_TYPE << "=' + this.options[this.selectedIndex].value)\">\n";

    uint a = 0;
    while (timeGroupTypes[a] != nullptr)
    {
      if (timeGroupTypeStr == timeGroupTypes[a])
      {
        timeGroupType = a;
        ostr1 << "<OPTION selected value=\"" << timeGroupTypes[a] << "\">" <<  timeGroupTypes[a] << "</OPTION>\n";
        session.setAttribute(ATTR_TIME_GROUP_TYPE,timeGroupTypeStr);
      }
      else
      {
        ostr1 << "<OPTION value=\"" <<  timeGroupTypes[a] << "\">" <<  timeGroupTypes[a] << "</OPTION>\n";
      }
      a++;
    }
    ostr1 << "</SELECT></TD>\n";

    if (timeStr.empty() &&  len > 0)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(0);
      std::string ft = g->getForecastTime();
      if (timeGroupStr.empty())
        timeGroupStr = ft.substr(0,timeGroupLen[timeGroupType]);
    }

    std::string prevTime = "14000101T0000";
    uint tCount = 0;

    if (timeGroupType > 0)
    {
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
                std::string ft = g->getForecastTime();
                timeGroupStrList.insert(ft.substr(0,timeGroupLen[timeGroupType]));
                tCount++;
              }
            }
          }
        }
      }
    }

    if (timeGroupStrList.size() > 1)
    {
      useTimeGroup = true;

      ostr1 << "<TD><SELECT id=\"yearselect\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() + "' + this.options[this.selectedIndex].value)\"";
      ostr1 << " >\n";

      for (auto it = timeGroupStrList.begin();it != timeGroupStrList.end(); ++it)
      {
        std::ostringstream out;
        out << "&" << ATTR_TIME_GROUP << "=" << *it << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr;
        std::string url = out.str();

        if (*it == timeGroupStr)
        {
          ostr1 << "<OPTION selected value=\"" <<  url << "\">" <<  *it << "</OPTION>\n";
          session.setAttribute(ATTR_TIME_GROUP,timeGroupStr);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  url << "\">" <<  *it << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT></TD>\n";
    }
    ostr1 << "</TR></TABLE></TD></TR>\n";



    prevTime = "14000101T0000";

    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Time (UTC):</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD><TABLE><TR><TD>\n";

    ostr3 << "<TABLE style=\"border-width:0;border-spacing:0;height:30;\"><TR>\n";


    T::ContentInfo *prevCont = nullptr;
    T::ContentInfo *currentCont = nullptr;
    T::ContentInfo *nextCont = nullptr;

    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiName_producer_generation_level_time);

      ostr1 << "<SELECT id=\"timeselect\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() + "' + this.options[this.selectedIndex].value)\">\n";

      std::string u;
      if (presentation == "Image" ||  presentation == "Map"  ||  presentation == "Streams")
      {
        u = "/grid-gui?session=" + session.getUrlParameter() + "&" + ATTR_PAGE + "=" + presentation;
      }

      uint daySwitch = 0;
      uint cc = 0;
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        //printf("**** TIME %s (%s)  geom=%u %u  ft=%u %u   fn=%d %d \n",g->getForecastTime(),prevTime.c_str(),g->mGeometryId,geometryId,forecastType,g->mForecastType,forecastNumber,g->mForecastNumber);

        if (g->mGeometryId == geometryId)
        {
          std::string ft = g->getForecastTime();
          if (prevTime < ft  &&  (!useTimeGroup  || (useTimeGroup  &&  ft.substr(0,timeGroupLen[timeGroupType]) == timeGroupStr)))
          {
            if (forecastType == g->mForecastType)
            {
              if (forecastNumber == g->mForecastNumber)
              {
                std::ostringstream out;
                out << "&" << ATTR_TIME << "=" << g->getForecastTime() << "&" << ATTR_FILE_ID << "=" << g->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr;
                std::string url = out.str();
                std::string uu = url;

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
                  {
                    ostr3 << "<TD style=\"text-align:center; font-size:12;width:30;background:#000000;color:#FFFFFF;\">UTC</TD>\n";
                    ostr3 << "<TD style=\"text-align:center; font-size:12;width:120;background:#F0F0F0;\" id=\"ftime\">" + timeStr + "</TD><TD style=\"width:1;\"> </TD>\n";
                  }

                  if (u > " ")
                  {
                    ostr3 << "<TD style=\"width:5; background:" << bg << ";\" ";
                    ostr3 << " onmouseout=\"this.style='width:5;background:"<< bg << ";'\"";
                    ostr3 << " onmouseover=\"this.style='width:5;height:30;background:#FF0000;'; setText('ftime','" << g->getForecastTime() << "');setText('flevel','" << g->mParameterLevel << "');setImage(document.getElementById('myimage'),'" << u << uu << "');\"";
                    ostr3 << " onClick=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << ";" << ATTR_TIME << "=" << g->getForecastTime() << ";" << ATTR_FILE_ID << "=" << g->mFileId << ";" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << ";" << ATTR_FORECAST_TYPE << "=" << g->mForecastType << ";" << ATTR_FORECAST_NUMBER << "=" << g->mForecastNumber << "');\" > </TD>\n";

                  }
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
      ostr1 << "<TD width=\"20\"><button type=\"button\">&lt;</button></TD>\n";

    if (nextCont != nullptr)
      ostr1 << "<TD width=\"20\"><button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_TIME << "=" << nextCont->getForecastTime() << "&" << ATTR_FILE_ID << "=" << nextCont->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << nextCont->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr + "');\">&gt;</button></TD>\n";
    else
      ostr1 << "<TD width=\"20\"><button type=\"button\">&gt;</button></TD>\n";

    ostr1 << "</TR></TABLE></TD></TR>\n";

    ostr3 << "<TD></TD></TR></TABLE>\n";



    T::ContentInfoList contentInfoListByLevels;
    if (!timeStr.empty())
      contentServer->getContentListByParameterAndGenerationId(0,generationId,T::ParamKeyTypeValue::FMI_NAME,parameterIdStr,levelId,0,1000000000,forecastType,forecastNumber,geometryId,timeStr,timeStr,0,contentInfoListByLevels);

    uint lCount = contentInfoListByLevels.getLength();

    std::string u;
    if (presentation == "Image" ||  presentation == "Map" ||  presentation == "Streams")
    {
      u = "/grid-gui?session=" + session.getUrlParameter() + "&" + ATTR_PAGE + "=" + presentation;
    }

    if (itsAnimationEnabled)
    {
      ostr4 << "<TABLE style=\"border-width:0;border-spacing:0;width:70;\">\n";
      ostr4 << "<TR><TD style=\"height:35;\"> </TD></TR>\n";
      ostr4 << "<TR><TD style=\"height:35;text-align:center; font-size:12;background:#000000;color:#FFFFFF;\">Level</TD></TR>\n";
      ostr4 << "<TR><TD style=\"text-align:center; font-size:12;background:#F0F0F0;\" id=\"flevel\">" << levelStr << "</TD></TR>\n";

      for (uint a=0; a<lCount; a++)
      {
        T::ContentInfo *g = contentInfoListByLevels.getContentInfoByIndex(a);

        std::ostringstream out;
        out << "&" << ATTR_TIME << "=" << timeStr << "&" << ATTR_FILE_ID << "=" << g->mFileId << "&" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr;
        std::string url = out.str();
        std::string uu = url;

        std::string bg = "#E0E0E0";
        if (g->mParameterLevel == level)
          bg = "#0000FF";

        if (u > " ")
        {
          ostr4 << "<TR style=\"height:5;\"><TD style=\" background:" << bg << ";\"";
          ostr4 << " onmouseout=\"this.style='background:"+bg+";'\"";
          ostr4 << " onmouseover=\"this.style='background:#FF0000;';setText('ftime','" << g->getForecastTime() << "');setText('flevel','" << g->mParameterLevel << "');setImage(document.getElementById('myimage'),'" << u << uu << "');\"";
          ostr4 << " onClick=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << ";" << ATTR_TIME << "=" << g->getForecastTime() << ";" << ATTR_FILE_ID << "=" << g->mFileId << ";" << ATTR_MESSAGE_INDEX << "=" << g->mMessageIndex << ";" << ATTR_FORECAST_TYPE << "=" << g->mForecastType << ";" << ATTR_FORECAST_NUMBER << "=" << g->mForecastNumber << ";" << ATTR_LEVEL << "=" << g->mParameterLevel << "');\"> </TD></TR>\n";
        }
        else
          ostr4 << "<TR style=\"height:5;\"><TD style=\" background:" << bg <<";\"> </TD></TR>\n";
      }
      ostr4 << "</TABLE>\n";
    }

    ostr1 << "<TR height=\"15\"><TD><HR/></TD></TR>\n";
    ostr1 << "<TR height=\"15\" style=\"font-size:12; width:100%;\"><TD>FMI Key:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD><INPUT type=\"text\" style=\"width:280px;background-color:#FFFF00;\" value=\"" << fmiKeyStr << "\"></TD></TR>\n";


    // ### Presentation:

    const char *modes[] = {"Image","Map","Streams","StreamsAnimation","Info","Table(sample)","Coordinates(sample)","Message",nullptr};

    ostr1 << "<TR height=\"15\"><TD><HR/></TD></TR>\n";
    ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Presentation:</TD></TR>\n";
    ostr1 << "<TR height=\"30\"><TD>\n";
    ostr1 << "<SELECT style=\"width:280px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_TIME << "=" << timeStr << "&" << ATTR_FILE_ID << "=" << fileIdStr << "&" << ATTR_MESSAGE_INDEX << "=" << messageIndexStr << "&" << ATTR_FORECAST_TYPE << "=" << forecastTypeStr << "&" << ATTR_FORECAST_NUMBER << "=" << forecastNumberStr << "&" << ATTR_PRESENTATION << "=' + this.options[this.selectedIndex].value)\">\n";

    a = 0;
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


    ostr1 << "</SELECT>\n";
    ostr1 << "</TD></TR>\n";



    if (presentation == "Image" || presentation == "Streams" || presentation == "StreamsAnimation")
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

      if (projections.find(projectionId) == projections.end())
        projectionId = geometryId;

      if (projections.size() > 0)
      {
        ostr1 << "<SELECT style=\"width:200px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PROJECTION_ID << "=' + this.options[this.selectedIndex].value)\">\n";

        for (auto it=projections.begin(); it!=projections.end(); ++it)
        {
          std::string st = std::to_string(*it);

          if (projectionId == *it || itsBlockedProjections.find(*it) == itsBlockedProjections.end())
          {
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
        }
        ostr1 << "</SELECT>\n";
      }

      const char *projectionLockTypes[] = {"","Lock",nullptr};

      ostr1 << "<SELECT style=\"width:70px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PROJECTION_LOCK << "=' + this.options[this.selectedIndex].value)\">\n";

      uint a = 0;
      while (projectionLockTypes[a] != nullptr)
      {
        if (projectionLockStr == projectionLockTypes[a])
        {
          ostr1 << "<OPTION selected value=\"" << projectionLockTypes[a] << "\">" <<  projectionLockTypes[a] << "</OPTION>\n";
          session.setAttribute(ATTR_PROJECTION_LOCK,projectionLockTypes[a]);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  projectionLockTypes[a] << "\">" <<  projectionLockTypes[a] << "</OPTION>\n";
        }
        a++;
      }
      ostr1 << "</SELECT>\n";

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

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Color map and opacity:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:220px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_COLOR_MAP << "=' + this.options[this.selectedIndex].value)\">\n";

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

      ostr1 << "<SELECT style=\"width:50px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_OPACITY << "=' + this.options[this.selectedIndex].value)\">\n";


      for (uint a=0; a<256; a++)
      {
        if (a == opacity)
        {
          ostr1 << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
          session.setAttribute(ATTR_OPACITY,a);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        }
      }
      ostr1 << "</SELECT>\n";

      ostr1 << "</TD></TR>\n";
    }



    if (presentation == "Image" || presentation == "Map" || presentation == "Streams" || presentation == "StreamsAnimation")
    {
      if ((colorMap.empty() || colorMap == "None") &&  presentation != "Streams" && presentation != "StreamsAnimation")
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

        ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Step, min and max length, color</TD></TR>\n";
        ostr1 << "<TR height=\"30\"><TD>\n";
        ostr1 << "<SELECT style=\"width:40px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_STEP << "=' + this.options[this.selectedIndex].value)\">\n";

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
        ostr1 << "<SELECT style=\"width:50px;\"onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_MIN_LENGTH << "=' + this.options[this.selectedIndex].value)\">\n";

        for (uint a=2; a<128; a=a+2)
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
        for (uint a=128; a<=2048; a=a+64)
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


        ostr1 << "<SELECT style=\"width:50px;\"onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_MAX_LENGTH << "=' + this.options[this.selectedIndex].value)\">\n";

        uint maxLength = toUInt32(maxLengthStr);
        for (uint a=8; a<128; a=a+4)
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
        for (uint a=128; a<=2048; a=a+64)
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

        ostr1 << "<SELECT style=\"width:120px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_STREAM_COLOR << "=' + this.options[this.selectedIndex].value)\">\n";

        for (auto it = itsColors.begin(); it != itsColors.end(); ++it)
        {
          if (streamColorStr == it->first)
          {
            ostr1 << "<OPTION selected value=\"" << it->first << "\">" <<  it->first << "</OPTION>\n";
            session.setAttribute(ATTR_STREAM_COLOR,streamColorStr);
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

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Coordinate lines and land border:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:280px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_COORDINATE_LINES << "=' + this.options[this.selectedIndex].value)\">\n";

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


      // ### Land border:

      ostr1 << "<SELECT style=\"width:280px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LAND_BORDER << "=' + this.options[this.selectedIndex].value)\">\n";

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




      // ### Land and sea colors:

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Land and sea colors:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";

      a = 0;
      ostr1 << "<SELECT style=\"width:80px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LAND_COLOR_POS << "=' + this.options[this.selectedIndex].value)\">\n";
      while (layerPosition[a] != NULL)
      {
        if (landcol_pos == a)
        {
          ostr1 << "<OPTION selected value=\"" << a << "\">" <<  layerPosition[a] << "</OPTION>\n";
          session.setAttribute(ATTR_LAND_COLOR_POS,a);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  a << "\">" << layerPosition[a] << "</OPTION>\n";
        }
        a++;
      }
      ostr1 << "</SELECT>\n";


      a = 0;
      ostr1 << "<SELECT style=\"width:190px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LAND_MASK << "=' + this.options[this.selectedIndex].value)\">\n";
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


      a = 0;
      ostr1 << "<SELECT style=\"width:80px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SEA_COLOR_POS << "=' + this.options[this.selectedIndex].value)\">\n";
      while (layerPosition[a] != NULL)
      {
        if (seacol_pos == a)
        {
          ostr1 << "<OPTION selected value=\"" << a << "\">" <<  layerPosition[a] << "</OPTION>\n";
          session.setAttribute(ATTR_SEA_COLOR_POS,a);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  a << "\">" << layerPosition[a] << "</OPTION>\n";
        }
        a++;
      }
      ostr1 << "</SELECT>\n";


      ostr1 << "<SELECT style=\"width:190px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SEA_MASK << "=' + this.options[this.selectedIndex].value)\">\n";
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


      // ### Land and sea topology

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Land/sea topography positions and intensity:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";

      a = 0;
      ostr1 << "<SELECT style=\"width:80px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LAND_SHADING_POS << "=' + this.options[this.selectedIndex].value)\">\n";
      while (layerPosition[a] != NULL)
      {
        if (landshade_pos == a)
        {
          ostr1 << "<OPTION selected value=\"" << a << "\">" <<  layerPosition[a] << "</OPTION>\n";
          session.setAttribute(ATTR_LAND_SHADING_POS,a);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  a << "\">" << layerPosition[a] << "</OPTION>\n";
        }

        a++;
      }
      ostr1 << "</SELECT>\n";


      ostr1 << "<SELECT style=\"width:55px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LAND_SHADING_LIGHT << "=' + this.options[this.selectedIndex].value)\">\n";

      for (uint t=0; t<=512; t=t+16)
      {
        if (landshade_light == t)
        {
          ostr1 << "<OPTION selected value=\"" << t << "\">" <<  t << "</OPTION>\n";
          session.setAttribute(ATTR_LAND_SHADING_LIGHT,t);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  t << "\">" << t << "</OPTION>\n";
        }

        a++;
      }
      ostr1 << "</SELECT>\n";

      ostr1 << "<SELECT style=\"width:55px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_LAND_SHADING_SHADOW << "=' + this.options[this.selectedIndex].value)\">\n";

      for (uint t=0; t<=512; t=t+16)
      {
        if (landshade_shadow == t)
        {
          ostr1 << "<OPTION selected value=\"" << t << "\">" <<  t << "</OPTION>\n";
          session.setAttribute(ATTR_LAND_SHADING_SHADOW,t);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  t << "\">" << t << "</OPTION>\n";
        }

        a++;
      }
      ostr1 << "</SELECT>\n";

      ostr1 << "</TD></TR>\n";
      ostr1 << "<TR><TD>\n";

      a = 0;
      ostr1 << "<SELECT style=\"width:80px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SEA_SHADING_POS << "=' + this.options[this.selectedIndex].value)\">\n";
      while (layerPosition[a] != NULL)
      {
        if (seashade_pos == a)
        {
          ostr1 << "<OPTION selected value=\"" << a << "\">" <<  layerPosition[a] << "</OPTION>\n";
          session.setAttribute(ATTR_SEA_SHADING_POS,a);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  a << "\">" << layerPosition[a] << "</OPTION>\n";
        }

        a++;
      }
      ostr1 << "</SELECT>\n";


      ostr1 << "<SELECT style=\"width:55px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SEA_SHADING_LIGHT << "=' + this.options[this.selectedIndex].value)\">\n";

      for (uint t=0; t<=512; t=t+16)
      {
        if (seashade_light == t)
        {
          ostr1 << "<OPTION selected value=\"" << t << "\">" <<  t << "</OPTION>\n";
          session.setAttribute(ATTR_SEA_SHADING_LIGHT,t);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  t << "\">" << t << "</OPTION>\n";
        }

        a++;
      }
      ostr1 << "</SELECT>\n";


      ostr1 << "<SELECT style=\"width:55px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_SEA_SHADING_SHADOW << "=' + this.options[this.selectedIndex].value)\">\n";

      for (uint t=0; t<=512; t=t+16)
      {
        if (seashade_shadow == t)
        {
          ostr1 << "<OPTION selected value=\"" << t << "\">" <<  t << "</OPTION>\n";
          session.setAttribute(ATTR_SEA_SHADING_SHADOW,t);
        }
        else
        {
          ostr1 << "<OPTION value=\"" <<  t << "\">" << t << "</OPTION>\n";
        }

        a++;
      }
      ostr1 << "</SELECT>\n";


      ostr1 << "</TD></TR>\n";


      // ### Missing value:

      ostr1 << "<TR height=\"15\" style=\"font-size:12;\"><TD>Missing Value:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD>\n";
      ostr1 << "<SELECT style=\"width:280px;\" onchange=\"getPage(this,parent,'/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_MISSING << "=' + this.options[this.selectedIndex].value)\">\n";

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


      ostr1 << "<TR height=\"15\"><TD><HR/></TD></TR>\n";

      // ## Value and units:

      ostr1 << "<TR height=\"15\" style=\"font-size:12; width:100%;\"><TD>Value and units:</TD></TR>\n";
      ostr1 << "<TR height=\"30\"><TD><INPUT style=\"width:200px;\" type=\"text\" id=\"gridValue\"><INPUT style=\"width:80px;\"type=\"text\" value=\"" << unitStr << "\"></TD></TR>\n";
    }


    ostr1 << "<TR height=\"50%\"><TD> </TD></TR>\n";

    // ## Download
    ostr1 << "<TR height=\"30\" style=\"font-size:16; font-weight:bold; width:280px; color:#000000; background:#D0D0D0; vertical-align:middle; text-align:center; \"><TD><a href=\"grid-gui?" << ATTR_PAGE << "=download&" << ATTR_FILE_ID << "=" << fileIdStr << "&" << ATTR_MESSAGE_INDEX << "=" << messageIndexStr << "\">Download</a></TD></TR>\n";
    ostr1 << "</TABLE>\n";



    if (itsAnimationEnabled  &&  (presentation == "Image" /*|| presentation == "Map"*/ || presentation == "Streams"))
    {
      ostr2 << "<TABLE>\n";
      ostr2 << "<TR><TD style=\"height:35; width:100%; vertical-align:middle; text-align:left; font-size:12;\">" << ostr3.str() << "</TD></TR>\n";
    }
    else
    {
      ostr2 << "<TABLE width=\"100%\" height=\"100%\">\n";
    }

    if (presentation == "Image")
    {
      ostr2 << "<TR><TD style=\"vertical-align:top;\"><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:1000;\" src=\"/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\" onclick=\"getImageCoords(event,this," << fileIdStr << "," << messageIndexStr << ",'" << presentation << "');\"/></TD></TR>";
    }
    else
    if (presentation == "Map")
    {
      ostr2 << "<TR><TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:1000;\" src=\"/grid-gui?session=" << session.getUrlParameter() << "&" << ATTR_PAGE << "=" << presentation << "\"/></TD></TR>";
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

    std::string aggregation;
    std::string processing;
    if (currentCont)
    {
      if (currentCont->mAggregationId > 0)
      {
        Identification::AggregationDef aggregationDef;
        if (Identification::gridDef.getFmiAggregationDef(currentCont->mAggregationId,aggregationDef))
        {
          aggregation = " / Aggregation: " + aggregationDef.mDescription;
          if (currentCont->mAggregationPeriod)
          {
            if ((currentCont->mAggregationPeriod % 60) == 0)
              aggregation = aggregation + " (" + std::to_string(currentCont->mAggregationPeriod/60) + " hours)";
            else
              aggregation = aggregation + " (" + std::to_string(currentCont->mAggregationPeriod) + " minutes)";
          }
        }
      }

      if (currentCont->mProcessingTypeId > 0)
      {
        Identification::ProcessingTypeDef processingTypeDef;
        if (Identification::gridDef.getFmiProcessingTypeDef(currentCont->mProcessingTypeId,processingTypeDef))
        {
          processing = " / Processing: " + processingTypeDef.mDescription;
          if (currentCont->mProcessingTypeValue1 != ParamValueMissing)
          {
            processing = processing  + " (" + std::to_string(currentCont->mProcessingTypeValue1);
            if (currentCont->mProcessingTypeValue2 != ParamValueMissing)
              processing = processing + ", " + std::to_string(currentCont->mProcessingTypeValue2) + ")";
            processing = processing + ")";
          }
        }
      }
    }

    ostr2 << "<TR><TD style=\"height:25; vertical-align:middle; text-align:left; font-size:12;\">" << paramDescription << aggregation << processing << "</TD></TR>\n";

    ostr2 << "</TABLE>\n";


    if (presentation == "Image" || presentation == "Map" || presentation == "Streams")
    {
      output << "<TABLE height=\"100%\">\n";
    }
    else
    {
      output << "<TABLE height=\"100%\" width=\"100%\">\n";
    }

    output << "<TR>\n";

    output << "<TD style=\"vertical-align:top; background:#C0C0C0; width:290;\">\n";
    output << ostr1.str();
    output << "</TD>\n";

    output << "<TD  style=\"vertical-align:top;\">\n";
    output << ostr2.str();
    output << "</TD>\n";

    if (itsAnimationEnabled  &&  (presentation == "Image" /*|| presentation == "Map"*/ || presentation == "Streams"))
    {
      output << "<TD style=\"vertical-align:top; width:70;\">\n";
      output << ostr4.str();
      output << "</TD>\n";
    }

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

    Fmi::DateTime t_now = Fmi::SecondClock::universal_time();
    Fmi::DateTime t_expires = t_now + Fmi::Seconds(expires_seconds);
    std::shared_ptr<Fmi::TimeFormatter> tformat(Fmi::TimeFormatter::create("http"));
    std::string cachecontrol = "public, max-age=" + Fmi::to_string(expires_seconds);
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
 * \brief This is an admin plugin
 */
// ----------------------------------------------------------------------

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
