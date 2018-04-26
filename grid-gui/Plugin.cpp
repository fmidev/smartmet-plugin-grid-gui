// ======================================================================
/*!
 * \brief SmartMet Grid Gui plugin implementation
 */
// ======================================================================

#include "Plugin.h"

#include <grid-files/common/GeneralFunctions.h>
#include <grid-files/identification/GridDef.h>
#include <spine/SmartMet.h>
#include <macgyver/TimeFormatter.h>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{

using namespace SmartMet::Spine;


const char *colorNames[] = {"none","grey","dark-grey","light-grey","black","white","blue","red","yellow",NULL};
uint colorValues[] = {0xFFFFFFFF,0x808080,0x404040,0xC0C0C0,0x000000,0xFFFFFF,0x0000FF,0xFF0000,0xFFFF00};

uint getColorValue(const char *colorName)
{
  try
  {
    uint c = 0;
    while (colorNames[c] != NULL)
    {
      if (strcasecmp(colorName,colorNames[c]) == 0)
        return colorValues[c];
      c++;
    }
    return 0xFFFFFFFF;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}






// ----------------------------------------------------------------------
/*!
 * \brief Plugin constructor
 */
// ----------------------------------------------------------------------

Plugin::Plugin(SmartMet::Spine::Reactor *theReactor, const char *theConfig)
    : SmartMetPlugin(), itsModuleName("GridGui")
{
  try
  {
    const char *configAttribute[] =
    {
        "smartmet.plugin.grid-gui.grid-files.configFile",
        "smartmet.plugin.grid-gui.land-sea-mask-file",
        NULL
    };

    itsReactor = theReactor;

    if (theReactor->getRequiredAPIVersion() != SMARTMET_API_VERSION)
      throw SmartMet::Spine::Exception(BCP, "GridGui plugin and Server API version mismatch");

    // Register the handler
    if (!theReactor->addContentHandler(
            this, "/grid-gui", boost::bind(&Plugin::callRequestHandler, this, _1, _2, _3)))
      throw SmartMet::Spine::Exception(BCP, "Failed to register GridGui request handler");


    itsConfigurationFile.readFile(theConfig);

    uint t=0;
    while (configAttribute[t] != NULL)
    {
      if (!itsConfigurationFile.findAttribute(configAttribute[t]))
      {
        SmartMet::Spine::Exception exception(BCP, "Missing configuration attribute!");
        exception.addParameter("File",theConfig);
        exception.addParameter("Attribute",configAttribute[t]);
        throw exception;
      }
      t++;
    }

    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.grid-files.configFile",itsGridConfigFile);
    itsConfigurationFile.getAttributeValue("smartmet.plugin.grid-gui.land-sea-mask-file",itsLandSeaMaskFile);


    Identification::gridDef.init(itsGridConfigFile.c_str());

    jpg_load(itsLandSeaMaskFile.c_str(),itsLandSeaMask);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
  try
  {
    auto engine = itsReactor->getSingleton("grid", NULL);
    if (!engine)
      throw Spine::Exception(BCP, "The 'grid-engine' unavailable!");

    itsGridEngine = reinterpret_cast<Engine::Grid::Engine*>(engine);


  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





// ----------------------------------------------------------------------
/*!
 * \brief Shutdown the plugin
 */
// ----------------------------------------------------------------------

void Plugin::shutdown()
{
  try
  {
    std::cout << "  -- Shutdown requested (grid-content)\n";
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}






bool Plugin::isLand(double lon,double lat)
{
  try
  {
    if (itsLandSeaMask.pixel == NULL)
      return false;

    if (lon >= 180)
      lon = lon - 360;

    if (lat >= 90)
      lat = lat - 90;

    int x = (int)round((lon+180)*10);
    int y = (int)round((lat+90)*10);

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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





void Plugin::saveMap(const char *imageFile,uint columns,uint rows,T::ParamValue_vec&  values,unsigned char hue,unsigned char saturation,unsigned char blur,uint landBorder,std::string landMask,std::string seaMask)
{
  try
  {
    uint landColor = getColorValue(landMask.c_str());
    uint seaColor = getColorValue(seaMask.c_str());

    bool showLandMask = false;
    if (landMask == "simple")
      showLandMask = true;

    bool showSeaMask = false;
    if (seaMask == "simple")
      showSeaMask = true;

    double maxValue = -1000000000;
    double minValue = 1000000000;

    uint sz = (uint)values.size();

    if (sz != (columns * rows))
    {
      printf("The number of values (%u) does not match to the grid size (%u x %u)1\n",sz,columns,rows);
      exit(-1);
    }

    for (uint t=0; t<sz; t++)
    {
      double val = values[t];
      if (val != ParamValueMissing)
      {
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

    double xd = 360/(double)width;
    double yd = 180/(double)height;

    double dd = maxValue - minValue;
    double step = dd / 200;

    uint gridColor = 0xFFFFFF;

    unsigned long *image = new unsigned long[width*height];

    //unsigned char hue = 30;
    //unsigned char saturation = 128;
    uint c = 0;
    int ss = 2;
    bool yLand[width];
    for (int x=0; x<width; x++)
      yLand[x] = false;

    for (int y=0; y<height; y++)
    {
      bool prevLand = false;
      for (int x=0; x<width; x++)
      {
        T::ParamValue val = values[c];
        //printf("Val(%u,%u) : %f\n",x,y,val);
        uint v = 200 - (uint)((val - minValue) / step);
        v = v / blur;
        v = v * blur;
        v = v + 55;
        uint col = hsv_to_rgb(hue,saturation,(unsigned char)v);

        double xc = xd*((double)x-((double)width/2));
        double yc = yd*((double)(height-y-1)-((double)height/2));

        //printf("%f,%f\n",xc,yc);

        if (val == ParamValueMissing)
          col = 0xE8E8E8;

        bool land = isLand(xc,yc);

        if (landColor != 0xFFFFFFFF)
        {
          if (land)
            col = landColor;
        }
        else
        {
          if (showLandMask)
          {
            if ((x % ss) == 0)
            {
              if (land)
              {
                if (val == ParamValueMissing)
                  col = 0xC8C8C8;
                else
                  col = hsv_to_rgb(0,0,(unsigned char)v);
              }
              else
              {
                if (val == ParamValueMissing)
                  col = 0xD8D8D8;
              }
            }
          }
        }

        if (seaColor != 0xFFFFFFFF)
        {
          if (!land)
            col = seaColor;
        }
        else
        {
          if (showSeaMask)
          {
            if ((x % ss) == 0)
            {
              if (!land)
              {
                if (val == ParamValueMissing)
                  col = 0xC8C8C8;
                else
                  col = hsv_to_rgb(0,0,(unsigned char)v);
              }
              else
              {
                if (val == ParamValueMissing)
                  col = 0xD8D8D8;
              }
            }
          }
        }

        if (landBorder != 0xFFFFFFFF)
        {
          if (land & (!prevLand || !yLand[x]))
            col = landBorder;

          if (!land)
          {
            if (prevLand  &&  x > 0  &&  image[y*width + x-1] != gridColor)
              image[y*width + x-1] = landBorder;

            if (yLand[x] &&  y > 0  && image[(y-1)*width + x] != gridColor)
              image[(y-1)*width + x] = landBorder;
          }
        }

        if ((x % xx) == 0  ||  (y % yy) == 0)
        {
          col = gridColor;
        }

        yLand[x] = land;
        prevLand = land;
        image[y*width + x] = col;
        c++;
      }
    }

    jpeg_save(imageFile,image,height,width,100);
    delete image;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP,"Operation failed!",NULL);
  }
}





void Plugin::saveImage(const char *imageFile,T::GridData&  gridData,T::Coordinate_vec& coordinates,T::Coordinate_vec& lineCoordinates,unsigned char hue,unsigned char saturation,unsigned char blur,uint coordinateLines,uint landBorder,std::string landMask,std::string seaMask)
{
  try
  {
    bool landSeaMask = true;
    if (coordinates.size() == 0)
      landSeaMask = false;

    uint landColor = getColorValue(landMask.c_str());
    uint seaColor = getColorValue(seaMask.c_str());

    bool showLandMask = false;
    if (landMask == "simple")
      showLandMask = true;

    bool showSeaMask = false;
    if (seaMask == "simple")
      showSeaMask = true;

    //printf("*** COORDINATES %u\n",(uint)coordinates.size());
    double maxValue = -1000000000;
    double minValue = 1000000000;

    int width = gridData.mColumns;
    int height = gridData.mRows;

    uint size = width*height;
    std::size_t sz = gridData.mValues.size();

    if (sz < (uint)size)
    {
      printf("ERROR: There are not enough values (= %u) for the grid (%u x %u)!\n",(uint)sz,width,height);
      return;
    }

    //std::set<unsigned long long> cList;

    for (uint t=0; t<size; t++)
    {
      double val = gridData.mValues[t];
      if (val != ParamValueMissing)
      {
        if (val < minValue)
          minValue = val;

        if (val > maxValue)
          maxValue = val;
      }
    }

    double dd = maxValue - minValue;
    double step = dd / 200;

    bool rotate = true;
    if ((int)coordinates.size() > (10*width)  &&  coordinates[0].y() < coordinates[10*width].y())
      rotate = true;
    else
      rotate = false;

    unsigned long *image = new unsigned long[size];
    uint c = 0;
    int ss = 2;

    bool yLand[width];
    for (int x=0; x<width; x++)
      yLand[x] = false;

    if (!rotate)
    {
      for (int y=0; y<height; y++)
      {
        bool prevLand = false;
        for (int x=0; x<width; x++)
        {
          T::ParamValue val = gridData.mValues[c];

          uint v = 200 - (uint)((val - minValue) / step);
          v = v / blur;
          v = v * blur;
          v = v + 55;
          uint col = hsv_to_rgb(hue,saturation,(unsigned char)v);

          if (val == ParamValueMissing)
            col = 0xE8E8E8;

          //printf("COORDINATES : %d,%d => %f,%f\n",x,y,coordinates[c].x(),coordinates[c].y());
          bool land = false;
          if (landSeaMask)
            land = isLand(coordinates[c].x(),coordinates[c].y());


          if (landColor != 0xFFFFFFFF)
          {
            if (landSeaMask && land)
              col = landColor;
          }
          else
          {
            if (showLandMask)
            {
              if ((x % ss) == 0)
              {
                if (landSeaMask && land)
                {
                  if (val == ParamValueMissing)
                    col = 0xC8C8C8;
                  else
                    col = hsv_to_rgb(0,0,(unsigned char)v);
                }
                else
                {
                  if (val == ParamValueMissing)
                    col = 0xD8D8D8;
                }
              }
            }
          }

          if (seaColor != 0xFFFFFFFF)
          {
            if (landSeaMask && !land)
              col = seaColor;
          }
          else
          {
            if (showSeaMask)
            {
              if ((x % ss) == 0)
              {
                if (landSeaMask && !land)
                {
                  if (val == ParamValueMissing)
                    col = 0xC8C8C8;
                  else
                    col = hsv_to_rgb(0,0,(unsigned char)v);
                }
                else
                {
                  if (val == ParamValueMissing)
                    col = 0xD8D8D8;
                }
              }
            }
          }

          if (landBorder != 0xFFFFFFFF)
          {
            if (land & (!prevLand || !yLand[x]))
              col = landBorder;

            if (!land)
            {
              if (prevLand  &&  x > 0)
                image[y*width + x-1] = landBorder;

              if (yLand[x] &&  y > 0)
                image[(y-1)*width + x] = landBorder;
            }
          }

      //    if (((int)coordinates[c].x() % 10) == 0  ||  ((int)coordinates[c].y() % 10) == 0)
      //      col = 0xFF0000;

          yLand[x] = land;
          prevLand = land;
          image[y*width + x] = col;
          c++;
        }
      }
    }
    else
    {
      for (int y=height-1; y>=0; y--)
      {
        bool prevLand = false;
        for (int x=0; x<width; x++)
        {
          T::ParamValue val = gridData.mValues[c];
          uint v = 200 - (uint)((val - minValue) / step);
          v = v / blur;
          v = v * blur;
          v = v + 55;
          uint col = hsv_to_rgb(hue,saturation,(unsigned char)v);

          if (val == ParamValueMissing)
            col = 0xE8E8E8;

          //printf("COORDINATES : %d,%d => %f,%f\n",x,y,coordinates[c].x(),coordinates[c].y());
          bool land = false;
          if (landSeaMask)
            land = isLand(coordinates[c].x(),coordinates[c].y());

          if (landColor != 0xFFFFFFFF)
          {
            if (landSeaMask && land)
              col = landColor;
          }
          else
          {
            if (showLandMask)
            {
              if ((x % ss) == 0)
              {
                if (landSeaMask && land)
                {
                  if (val == ParamValueMissing)
                    col = 0xC8C8C8;
                  else
                    col = hsv_to_rgb(0,0,(unsigned char)v);
                }
                else
                {
                  if (val == ParamValueMissing)
                    col = 0xD8D8D8;
                }
              }
            }
          }

          if (seaColor != 0xFFFFFFFF)
          {
            if (landSeaMask && !land)
              col = seaColor;
          }
          else
          {
            if (showSeaMask)
            {
              if ((x % ss) == 0)
              {
                if (landSeaMask && !land)
                {
                  if (val == ParamValueMissing)
                    col = 0xC8C8C8;
                  else
                    col = hsv_to_rgb(0,0,(unsigned char)v);
                }
                else
                {
                  if (val == ParamValueMissing)
                    col = 0xD8D8D8;
                }
              }
            }
          }

          if (landBorder != 0xFFFFFFFF)
          {
            if (land & (!prevLand || !yLand[x]))
              col = landBorder;

            if (!land)
            {
              if (prevLand  &&  x > 0)
                image[y*width + x-1] = landBorder;

              if (yLand[x] &&  (y+1) < height)
                image[(y+1)*width + x] = landBorder;
            }
          }
/*
          int xx = (int)round(coordinates[c].x());
          int yy = (int)round(coordinates[c].y());

          if ((xx % 10) == 0  &&  (yy % 10) == 0)
          {
            unsigned long long key = (((unsigned long long)xx) << 32) + (unsigned long long)yy;
            if (cList.find(key) == cList.end())
            {
              col = 0xFF0000;
              cList.insert(key);
            }
          }

          if (((int)round(coordinates[c].x()*10) % 100) == 0  ||  ((int)round(coordinates[c].y()*10) % 100) == 0)
            col = 0xFF0000;
*/

          yLand[x] = land;
          prevLand = land;
          image[y*width + x] = col;
          c++;
        }
      }
    }

    if (coordinateLines != 0xFFFFFFFF  &&  lineCoordinates.size() > 0)
    {
      if (!rotate)
      {
        for (auto it = lineCoordinates.begin(); it != lineCoordinates.end(); ++it)
          image[(int)floor(it->y())*width + (int)floor(it->x())] = coordinateLines;
      }
      else
      {
        for (auto it = lineCoordinates.begin(); it != lineCoordinates.end(); ++it)
          image[(height-(int)floor(it->y())-1)*width + (int)floor(it->x())] = coordinateLines;
      }
    }

    jpeg_save(imageFile,image,height,width,100);
    delete image;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP,"Operation failed!",NULL);
  }
}





void Plugin::saveTimeSeries(const char *imageFile,std::vector<T::ParamValue>& valueList,int idx,std::set<int> dayIdx)
{
  try
  {
    T::ParamValue maxValue = -1000000000;
    T::ParamValue minValue = 1000000000;

    int len = (int)valueList.size();


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


    unsigned long *image = new unsigned long[size];
    memset(image,0xFF,size*sizeof(unsigned long));

    for (int x=0; x<len; x++)
    {
      int xp = x*3;
      int yp = (int)(dd*(valueList[x]-minValue)) + 1;
      //uint col = 0x404040;

      uint v = 200 - (uint)((valueList[x]-minValue) / step);
      v = v + 55;
      uint col = hsv_to_rgb(0,0,(unsigned char)v);

      if (x == idx)
        col = 0xFF0000;

      for (uint w=0; w<3; w++)
      {
        for (int y=0; y<yp; y++)
        {
          int pos = ((height-y-1)*width + xp+w);
          if (pos >=0  && pos < (int)size)
            image[pos] = col;
        }
      }

      if (dayIdx.find(x) != dayIdx.end())
      {
        for (int y=0; y<height; y++)
        {
          int pos = (y*width + xp);
          if (pos >=0  && pos < (int)size)
            image[pos] = 0xA0A0A0;
        }
      }

    }
    jpeg_save(imageFile,image,height,width,100);
    delete image;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP,"Operation failed!",NULL);
  }
}





bool Plugin::page_info(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    uint flags = 0;
    std::string fileIdStr = "";
    std::string messageIndexStr = "0";

    boost::optional<std::string> v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      messageIndexStr = *v;

    if (fileIdStr.empty())
      return true;


    std::ostringstream ostr;

    //printf("CONTENT: %s:%s\n",fileIdStr.c_str(),messageIndexStr.c_str());
    T::ContentInfo contentInfo;
    int result = contentServer->getContentInfo(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),contentInfo);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getContentInfo : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    //contentInfo.print(std::cout,0,0);

    T::FileInfo fileInfo;
    result = contentServer->getFileInfoById(0,contentInfo.mFileId,fileInfo);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getFileInfoById : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    T::GenerationInfo generationInfo;
    result = contentServer->getGenerationInfoById(0,contentInfo.mGenerationId,generationInfo);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getGenerationInfoById : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    T::ProducerInfo producerInfo;
    result = contentServer->getProducerInfoById(0,contentInfo.mProducerId,producerInfo);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getProducerInfoById : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    T::AttributeList attributeList;
    result = dataServer->getGridAttributeList(0,contentInfo.mFileId,contentInfo.mMessageIndex,flags,attributeList);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "ERROR: getGridAttributeList : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
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

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">File</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Id</TD><TD>" << fileInfo.mFileId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Name</TD><TD>" << fileInfo.mName << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">Parameter</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Message index</TD><TD>" << contentInfo.mMessageIndex << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Forecast time</TD><TD>" << contentInfo.mForecastTime << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Level</TD><TD>" << contentInfo.mParameterLevel << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">FMI identifier</TD><TD>" << contentInfo.mFmiParameterId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">FMI name</TD><TD>" << contentInfo.mFmiParameterName << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">FMI level identifier</TD><TD>" << (int)contentInfo.mFmiParameterLevelId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">FMI units</TD><TD>" << contentInfo.mFmiParameterUnits << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">GRIB identifier</TD><TD>" << contentInfo.mGribParameterId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">GRIB1 level identifier</TD><TD>" << (int)contentInfo.mGrib1ParameterLevelId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">GRIB2 level identifier</TD><TD>" << (int)contentInfo.mGrib2ParameterLevelId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">GRIB units</TD><TD>" << contentInfo.mGribParameterUnits << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Newbase identifier</TD><TD>" << contentInfo.mNewbaseParameterId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Newbase name</TD><TD>" << contentInfo.mNewbaseParameterName << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">CDM identifier</TD><TD>" << contentInfo.mCdmParameterId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">CDM name</TD><TD>" << contentInfo.mCdmParameterName << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Forecast type</TD><TD>" << (int)contentInfo.mForecastType << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Forecast number</TD><TD>" << (int)contentInfo.mForecastNumber << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Geometry identifier</TD><TD>" << (uint)contentInfo.mGeometryId << "</TD></TR>\n";

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
    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





bool Plugin::page_table(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string presentation = "table(sample)";
    char tmp[1000];

    boost::optional<std::string> v = theRequest.getParameter("presentation");
    if (v)
      presentation = *v;

    v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      messageIndexStr = *v;


    if (fileIdStr.empty())
      return true;

    std::ostringstream ostr;

    uint flags = 0;
    T::GridData gridData;
    int result = dataServer->getGridData(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),flags,gridData);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridData()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    T::Coordinate_vec coordinates = Identification::gridDef.getGridCoordinatesByGeometryId(gridData.mGeometryId);
    /*
    T::GridCoordinates coordinates;
    result = dataServer->getGridCoordinates(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),flags,T::CoordinateType::ORIGINAL_COORDINATES,coordinates);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridCoordinates()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }
    */


    uint c = 0;
    uint height = gridData.mRows;
    uint width = gridData.mColumns;

    if (presentation == "table(sample)")
    {
      if (width > 100)
        width = 100;

      if (height > 100)
        height = 100;
    }

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
      sprintf(tmp,"%.3f",coordinates[x].x());
      ostr << "<TD>" << tmp << "</TD>";
    }
    ostr << "</TR>\n";


    // ### Rows:

    for (uint y=0; y<height; y++)
    {
      c = y*gridData.mColumns;

      // ### Row index and Y coordinate:

      sprintf(tmp,"%.3f",coordinates[c].y());
      ostr << "<TR><TD bgColor=\"#E0E0E0\">" << y << "</TD><TD bgColor=\"#D0D0D0\">" << tmp << "</TD>";

      // ### Columns:

      for (uint x=0; x<width; x++)
      {
        ostr << "<TD>";
        if (c < (uint)gridData.mValues.size())
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

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





bool Plugin::page_coordinates(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string presentation = "coordinates(sample)";
    char tmp[1000];

    boost::optional<std::string> v = theRequest.getParameter("presentation");
    if (v)
      presentation = *v;

    v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      messageIndexStr = *v;


    if (fileIdStr.empty())
      return true;

    std::ostringstream ostr;

    uint flags = 0;

    T::GridCoordinates coordinates;
    int result = dataServer->getGridCoordinates(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),flags,T::CoordinateType::LATLON_COORDINATES,coordinates);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridCoordinates()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }


    uint c = 0;
    uint height = coordinates.mRows;
    uint width = coordinates.mColumns;

    if (presentation == "coordinates(sample)")
    {
      if (width > 100)
        width = 100;

      if (height > 100)
        height = 100;
    }

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
        if (c < (uint)coordinates.mCoordinateList.size())
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

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





bool Plugin::page_value(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    uint fileId = 0;
    uint messageIndex = 0;
    double xPos = 0;
    double yPos = 0;

    boost::optional<std::string> v = theRequest.getParameter("fileId");
    if (v)
      fileId = atoll(v->c_str());

    v = theRequest.getParameter("messageIndex");
    if (v)
      messageIndex = atoll(v->c_str());

    v = theRequest.getParameter("x");
    if (v)
      xPos = atof(v->c_str());

    v = theRequest.getParameter("y");
    if (v)
      yPos = atof(v->c_str());

    if (fileId == 0)
      return true;


    T::ContentInfo contentInfo;
    if (contentServer->getContentInfo(0,fileId,messageIndex,contentInfo) != 0)
      return true;

    if (contentInfo.mGeometryId == 0)
      return true;

    uint cols = 0;
    uint rows = 0;

    if (!Identification::gridDef.getGridDimensionsByGeometryId(contentInfo.mGeometryId,cols,rows))
      return true;

    uint height = rows;
    uint width = cols;

    bool reverseXDirection = false;
    bool reverseYDirection = false;

    if (!Identification::gridDef.getGridDirectionsByGeometryId(contentInfo.mGeometryId,reverseXDirection,reverseYDirection))
      return true;
/*
    T::Coordinate_vec coordinates;
    coordinates = Identification::gridDef.getGridLatLonCoordinatesByGeometryId(contentInfo.mGeometryId);

    bool rotate = true;
    if ((int)coordinates.size() > (int)(10*width)  &&  coordinates[0].y() < coordinates[10*width].y())
      rotate = true;
    else
      rotate = false;
*/

    double xx = (double)(xPos * (double)width);
    double yy = (double)(yPos * (double)height);

    if (!reverseYDirection)
      yy = (double)height - (double)(yPos * (double)height);

    if (reverseXDirection)
      xx = (double)width - (double)(xPos * (double)width);

    uint flags = 0;
    T::ParamValue value;
    dataServer->getGridValueByPoint(0,fileId,messageIndex,flags,T::CoordinateType::GRID_COORDINATES,xx,yy,T::AreaInterpolationMethod::Linear,value);

    if (value != ParamValueMissing)
      theResponse.setContent(std::to_string(value));
    else
      theResponse.setContent(std::string("Not available"));

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





bool Plugin::page_timeseries(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    uint fileId = 0;
    uint messageIndex = 0;
    std::string presentation = "image";
    double xPos = 0;
    double yPos = 0;

    boost::optional<std::string> v = theRequest.getParameter("presentation");
    if (v)
      presentation = *v;

    v = theRequest.getParameter("fileId");
    if (v)
      fileId = atoll(v->c_str());

    v = theRequest.getParameter("messageIndex");
    if (v)
      messageIndex = atoll(v->c_str());

    v = theRequest.getParameter("x");
    if (v)
      xPos = atof(v->c_str());

    v = theRequest.getParameter("y");
    if (v)
      yPos = atof(v->c_str());

    if (fileId == 0)
      return true;


    T::ContentInfo contentInfo;
    if (contentServer->getContentInfo(0,fileId,messageIndex,contentInfo) != 0)
      return true;

    if (contentInfo.mGeometryId == 0)
      return true;

    uint cols = 0;
    uint rows = 0;

    if (!Identification::gridDef.getGridDimensionsByGeometryId(contentInfo.mGeometryId,cols,rows))
      return true;

    uint height = rows;
    uint width = cols;

    double xx = (double)(xPos * (double)width);
    double yy = (double)(yPos * (double)height);

    //if (presentation == "image(rotated)")
    //  yy = height-yy;

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByParameterAndGenerationId(0,contentInfo.mGenerationId,T::ParamKeyType::FMI_NAME,contentInfo.mFmiParameterName,T::ParamLevelIdType::FMI,contentInfo.mFmiParameterLevelId,contentInfo.mParameterLevel,contentInfo.mParameterLevel,-2,-2,-2,"19000101T000000","23000101T000000",0,contentInfoList);

    contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiId_level_starttime_file_message);

    std::vector <T::ParamValue> valueList;

    int idx = -1;
    //std::ostringstream ostr;
    std::set<int> dayIdx;

    uint flags = 0;
    uint c = 0;
    uint len = contentInfoList.getLength();
    for (uint t=0; t<len; t++)
    {
      T::ContentInfo *info = contentInfoList.getContentInfoByIndex(t);

      if (info->mGeometryId == contentInfo.mGeometryId  &&  info->mForecastType == contentInfo.mForecastType  &&  info->mForecastNumber == contentInfo.mForecastNumber)
      {
        T::ParamValue value;
        if (dataServer->getGridValueByPoint(0,info->mFileId,info->mMessageIndex,flags,T::CoordinateType::GRID_COORDINATES,xx,yy,T::AreaInterpolationMethod::Linear,value) == 0)
        {
          if (value != ParamValueMissing)
          {
            if (info->mFileId == fileId  &&  info->mMessageIndex == messageIndex)
              idx = (int)c;

            if (strstr(info->mForecastTime.c_str(),"T000000") != NULL)
              dayIdx.insert(t);

            valueList.push_back(value);
            c++;
          }
        }
      }
    }

    char fname[200];
    sprintf(fname,"/tmp/image_%llu.jpg",getTime());

    saveTimeSeries(fname,valueList,idx,dayIdx);

    long long sz = getFileSize(fname);
    if (sz > 0)
    {
      char buf[10000];
      std::vector<char> *content = new std::vector<char>();
      content->reserve(sz);

      boost::shared_ptr<std::vector<char> > sContent;
      sContent.reset(content);

      FILE *file = fopen(fname,"r");
      if (file != NULL)
      {
        while (!feof(file))
        {
          int n = fread(buf,1,10000,file);
          if (n > 0)
          {
            for (int t=0; t<n; t++)
            {
              content->push_back(buf[t]);
            }
          }
        }
        fclose(file);
        remove(fname);

        theResponse.setHeader("Content-Type","image/jpg");
        theResponse.setContent(sContent);
      }
      return true;
    }
    else
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "Image does not exist!\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





bool Plugin::page_image(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string hueStr = "128";
    std::string saturationStr = "60";
    std::string presentation = "image";
    std::string blurStr = "1";
    std::string geometryIdStr = "0";
    std::string coordinateLinesStr = "dark-grey";
    std::string landBorderStr = "dark-grey";
    std::string landMaskStr = "none";
    std::string seaMaskStr = "none";

    boost::optional<std::string> v;
    v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      messageIndexStr = *v;

    v = theRequest.getParameter("geometryId");
    if (v)
      geometryIdStr = *v;

    v = theRequest.getParameter("hue");
    if (v)
      hueStr = *v;

    v = theRequest.getParameter("saturation");
    if (v)
      saturationStr = *v;

    v = theRequest.getParameter("blur");
    if (v)
      blurStr = *v;

    v = theRequest.getParameter("coordinateLines");
    if (v)
      coordinateLinesStr = *v;

    v = theRequest.getParameter("landBorder");
    if (v)
      landBorderStr = *v;

    v = theRequest.getParameter("landMask");
    if (v)
      landMaskStr = *v;

    v = theRequest.getParameter("seaMask");
    if (v)
      seaMaskStr = *v;

    uint flags = 0;
    T::GridData gridData;

    int result = dataServer->getGridData(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),flags,gridData);

    if (result != 0)
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridData()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    uint landBorder = getColorValue(landBorderStr.c_str());
    uint coordinateLines = getColorValue(coordinateLinesStr.c_str());

    T::Coordinate_vec coordinates;
    if (gridData.mGeometryId != 0)
      coordinates = Identification::gridDef.getGridLatLonCoordinatesByGeometryId(gridData.mGeometryId);
    else
      coordinates = Identification::gridDef.getGridLatLonCoordinatesByGeometryId(atoi(geometryIdStr.c_str()));


    T::Coordinate_vec lineCoordinates;
    if (coordinateLines != 0xFFFFFFFF)
    {
      if (gridData.mGeometryId != 0)
        lineCoordinates = Identification::gridDef.getGridLatLonCoordinateLinePointsByGeometryId(gridData.mGeometryId);
      else
        lineCoordinates = Identification::gridDef.getGridLatLonCoordinateLinePointsByGeometryId(atoi(geometryIdStr.c_str()));
    }

    /*
    //T::GridCoordinates coordinates;
    result = dataServer->getGridCoordinates(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),flags,T::CoordinateType::LATLON_COORDINATES,coordinates);
    if (result != 0)
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridCoordinates()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }
   */


    char fname[200];
    sprintf(fname,"/tmp/image_%llu.jpg",getTime());
    saveImage(fname,gridData,coordinates,lineCoordinates,(unsigned char)atoi(hueStr.c_str()),(unsigned char)atoi(saturationStr.c_str()),(unsigned char)atoi(blurStr.c_str()),coordinateLines,landBorder,landMaskStr,seaMaskStr);

    long long sz = getFileSize(fname);
    if (sz > 0)
    {
      char buf[10000];
      std::vector<char> *content = new std::vector<char>();
      content->reserve(sz);

      boost::shared_ptr<std::vector<char>> sContent;
      sContent.reset(content);

      FILE *file = fopen(fname,"r");
      if (file != NULL)
      {
        while (!feof(file))
        {
          int n = fread(buf,1,10000,file);
          if (n > 0)
          {
            for (int t=0; t<n; t++)
            {
              content->push_back(buf[t]);
            }
          }
        }
        fclose(file);
        remove(fname);

        theResponse.setHeader("Content-Type","image/jpg");
        theResponse.setContent(sContent);
      }
      return true;
    }
    else
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "Image does not exist!\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





bool Plugin::page_map(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    auto dataServer = itsGridEngine->getDataServer_sptr();

    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string hueStr = "128";
    std::string saturationStr = "60";
    std::string blurStr = "1";
    std::string landBorderStr = "dark-grey";
    std::string landMaskStr = "none";
    std::string seaMaskStr = "none";

    boost::optional<std::string> v;
    v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      messageIndexStr = *v;


    v = theRequest.getParameter("hue");
    if (v)
      hueStr = *v;

    v = theRequest.getParameter("saturation");
    if (v)
      saturationStr = *v;

    v = theRequest.getParameter("blur");
    if (v)
      blurStr = *v;

    v = theRequest.getParameter("landBorder");
    if (v)
      landBorderStr = *v;

    v = theRequest.getParameter("landMask");
    if (v)
      landMaskStr = *v;

    v = theRequest.getParameter("seaMask");
    if (v)
      seaMaskStr = *v;

    uint columns = 1800;
    uint rows = 900;
    uint flags = 0;
    T::ParamValue_vec values;

    int result = dataServer->getGridValueVectorByRectangle(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),flags,T::CoordinateType::LATLON_COORDINATES,columns,rows,-180,90,360/(double)columns,-180/(double)rows,T::AreaInterpolationMethod::Nearest,values);
    if (result != 0)
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridValuesByArea()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    uint landBorder = getColorValue(landBorderStr.c_str());

    char fname[200];
    sprintf(fname,"/tmp/image_%llu.jpg",getTime());
    saveMap(fname,columns,rows,values,(unsigned char)atoi(hueStr.c_str()),(unsigned char)atoi(saturationStr.c_str()),(unsigned char)atoi(blurStr.c_str()),landBorder,landMaskStr,seaMaskStr);

    long long sz = getFileSize(fname);
    if (sz > 0)
    {
      char buf[10000];
      std::vector<char> *content = new std::vector<char>();
      content->reserve(sz);

      boost::shared_ptr<std::vector<char> > sContent;
      sContent.reset(content);

      FILE *file = fopen(fname,"r");
      if (file != NULL)
      {
        while (!feof(file))
        {
          int n = fread(buf,1,10000,file);
          if (n > 0)
          {
            for (int t=0; t<n; t++)
            {
              content->push_back(buf[t]);
            }
          }
        }
        fclose(file);
        remove(fname);

        theResponse.setHeader("Content-Type","image/jpg");
        theResponse.setContent(sContent);
      }
      return true;
    }
    else
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "Map does not exist!\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





void Plugin::getLevelIds(T::ContentInfoList& contentInfoList,std::set<int>& levelIds)
{
  try
  {
    uint len = contentInfoList.getLength();

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if (levelIds.find(g->mFmiParameterLevelId) == levelIds.end())
      {
        levelIds.insert(g->mFmiParameterLevelId);
      }
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





void Plugin::getLevels(T::ContentInfoList& contentInfoList,int levelId,std::set<int>& levels)
{
  try
  {
    uint len = contentInfoList.getLength();

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if (levelId == g->mFmiParameterLevelId)
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





void Plugin::getForecastTypes(T::ContentInfoList& contentInfoList,int levelId,int level,std::set<int>& forecastTypes)
{
  try
  {
    uint len = contentInfoList.getLength();

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if (levelId == g->mFmiParameterLevelId)
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





void Plugin::getForecastNumbers(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,std::set<int>& forecastNumbers)
{
  try
  {
    uint len = contentInfoList.getLength();

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if (levelId == g->mFmiParameterLevelId)
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





void Plugin::getGeometries(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,int forecastNumber,std::set<int>& geometries)
{
  try
  {
    uint len = contentInfoList.getLength();

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if (levelId == g->mFmiParameterLevelId)
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}







void Plugin::getGenerations(T::GenerationInfoList& generationInfoList,std::set<std::string>& generations)
{
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





bool Plugin::page_main(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();

    std::string producerIdStr = "";
    std::string generationIdStr = "";
    std::string geometryIdStr = "";
    std::string parameterIdStr = "";
    std::string parameterLevelStr = "";
    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string parameterLevelIdStr;
    std::string hueStr = "128";
    std::string forecastTypeStr = "";
    std::string forecastNumberStr = "";
    std::string saturationStr = "60";
    std::string blurStr = "1";
    std::string coordinateLinesStr = "dark-grey";
    std::string landBorderStr = "dark-grey";
    std::string landMaskStr = "none";
    std::string seaMaskStr = "none";
    std::string startTime = "";
    std::string unitStr = "";
    std::string presentation = "image";

    boost::optional<std::string> v;
    v = theRequest.getParameter("producerId");
    if (v)
      producerIdStr = *v;

    v = theRequest.getParameter("generationId");
    if (v)
      generationIdStr = *v;

    v = theRequest.getParameter("geometryId");
    if (v)
      geometryIdStr = *v;

    v = theRequest.getParameter("parameterId");
    if (v)
      parameterIdStr = *v;

    v = theRequest.getParameter("levelId");
    if (v)
      parameterLevelIdStr = *v;

    v = theRequest.getParameter("forecastType");
    if (v)
      forecastTypeStr = *v;

    v = theRequest.getParameter("forecastNumber");
    if (v)
      forecastNumberStr = *v;

    v = theRequest.getParameter("level");
    if (v)
      parameterLevelStr = *v;

    v = theRequest.getParameter("presentation");
    if (v)
      presentation = *v;

    v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      messageIndexStr = *v;

    v = theRequest.getParameter("start");
    if (v)
      startTime = *v;

    v = theRequest.getParameter("hue");
    if (v)
      hueStr = *v;

    v = theRequest.getParameter("saturation");
    if (v)
      saturationStr = *v;

    v = theRequest.getParameter("blur");
    if (v)
      blurStr = *v;

    v = theRequest.getParameter("coordinateLines");
    if (v)
      coordinateLinesStr = *v;

    v = theRequest.getParameter("landBorder");
    if (v)
      landBorderStr = *v;

    v = theRequest.getParameter("landMask");
    if (v)
      landMaskStr = *v;

    v = theRequest.getParameter("seaMask");
    if (v)
      seaMaskStr = *v;

    std::ostringstream ostr;

    ostr << "<HTML>\n";
    ostr << "<BODY>\n";

    ostr << "<SCRIPT>\n";

    ostr << "var backColor;\n";
    ostr << "var invisible = '#fefefe';\n";
    ostr << "var buttonColor = '#808080';\n";

    ostr << "function getPage(obj,frm,url)\n";
    ostr << "{\n";
    ostr << "  frm.location.href=url;\n";
    ostr << "}\n";

    ostr << "function setImage(img,url)\n";
    ostr << "{\n";
    ostr << "  img.src = url;\n";
    ostr << "}\n";

    ostr << "function mouseOver(obj)\n";
    ostr << "{\n";
    ostr << "  if (obj.bgColor != invisible)\n";
    ostr << "  {\n";
    ostr << "    backColor = obj.bgColor;\n";
    ostr << "    obj.bgColor='#FF8040';\n";
    ostr << "  }\n";
    ostr << "}\n";

    ostr << "function mouseOut(obj)\n";
    ostr << "{\n";
    ostr << "  if (obj.bgColor != invisible)\n";
    ostr << "  {\n";
    ostr << "    obj.bgColor=backColor;\n";
    ostr << "  }\n";
    ostr << "}\n";

    ostr << "function httpGet(theUrl)\n";
    ostr << "{\n";
    ostr << "  var xmlHttp = new XMLHttpRequest();\n";
    ostr << "  xmlHttp.open(\"GET\", theUrl, false );\n";
    ostr << "  xmlHttp.send( null );\n";
    ostr << "  return xmlHttp.responseText;\n";
    ostr << "}\n";

    ostr << "function getImageCoords(event,img,fileId,messageIndex,presentation) {\n";
    ostr << "  var posX = event.offsetX?(event.offsetX):event.pageX-img.offsetLeft;\n";
    ostr << "  var posY = event.offsetY?(event.offsetY):event.pageY-img.offsetTop;\n";
    ostr << "  var prosX = posX / img.width;\n";
    ostr << "  var prosY = posY / img.height;\n";
    ostr << "  var url = \"/grid-gui?page=value&presentation=\" + presentation + \"&fileId=\" + fileId + \"&messagIndex=\" + messageIndex + \"&x=\" + prosX + \"&y=\" + prosY;\n";

    //ostr << "  document.getElementById('gridValue').value = url;\n";
    ostr << "  var txt = httpGet(url);\n";
    ostr << "  document.getElementById('gridValue').value = txt;\n";

    //ostr << "  var url2 = \"/grid-gui?page=timeseries&presentation=\" + presentation + \"&fileId=\" + fileId + \"&messagIndex=\" + messageIndex + \"&x=\" + prosX + \"&y=\" + prosY;\n";
    //ostr << "  document.getElementById('timeseries').src = url2;\n";

    //ostr << "  alert(\"You clicked at: (\"+posX+\",\"+posY+\")\");\n";
    ostr << "}\n";
    ostr << "</SCRIPT>\n";


    ostr << "<TABLE width=\"100%\" height=\"100%\">\n";
    ostr << "<TR><TD  bgcolor=\"#C0C0C0\" width=\"180\" rowspan=\"2\">\n";

    ostr << "<TABLE width=\"100%\" height=\"100%\">\n";


    // ### Producers:

    T::ProducerInfoList producerInfoList;
    contentServer->getProducerInfoList(0,producerInfoList);
    uint len = producerInfoList.getLength();
    producerInfoList.sortByName();
    uint pid = (uint)atoi(producerIdStr.c_str());

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Producer:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&producerId=' + this.options[this.selectedIndex].value)\">\n";
      for (uint t=0; t<len; t++)
      {
        T::ProducerInfo *p = producerInfoList.getProducerInfoByIndex(t);

        if (pid == 0)
        {
          pid = p->mProducerId;
          producerIdStr = std::to_string(pid);
        }

        if (pid == p->mProducerId)
          ostr << "<OPTION selected value=\"" <<  p->mProducerId << "\">" <<  p->mName << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  p->mProducerId << "\">" <<  p->mName << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### Generations:

    T::GenerationInfoList generationInfoList;
    contentServer->getGenerationInfoListByProducerId(0,pid,generationInfoList);
    uint gid = (uint)atoi(generationIdStr.c_str());

    if (generationInfoList.getGenerationInfoById(gid) == NULL)
      gid = 0;

    std::set<std::string> generations;
    getGenerations(generationInfoList,generations);


    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Generation:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (generations.size() > 0)
    {
      std::string disabled = "";
      if (generations.size() == 1)
        disabled = "disabled";

      ostr << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&producerId=" + producerIdStr + "&geometryId=" + geometryIdStr + "&generationId=' + this.options[this.selectedIndex].value)\">\n";

      for (auto it = generations.rbegin(); it != generations.rend(); ++it)
      {
        T::GenerationInfo *g = generationInfoList.getGenerationInfoByName(*it);
        if (g != NULL)
        {
          if (gid == 0)
          {
            gid = g->mGenerationId;
            generationIdStr = std::to_string(gid);
          }

          if (gid == g->mGenerationId)
            ostr << "<OPTION selected value=\"" <<  g->mGenerationId << "\">" <<  g->mName << "</OPTION>\n";
          else
            ostr << "<OPTION value=\"" <<  g->mGenerationId << "\">" <<  g->mName << "</OPTION>\n";
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### Parameters:

    std::set<std::string> paramKeyList;
    contentServer->getContentParamKeyListByGenerationId(0,gid,T::ParamKeyType::FMI_NAME,paramKeyList);

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Parameter:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (paramKeyList.size() > 0)
    {
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it=paramKeyList.begin(); it!=paramKeyList.end(); ++it)
      {
        std::string pId = *it;
        std::string pName = *it;
        Identification::FmiParameterDef def;

        if (Identification::gridDef.getFmiParameterDefByName(*it,def))
        {
          pId = def.mParameterName;
          pName = def.mParameterName;
          if (parameterIdStr == pId || parameterIdStr.empty())
            unitStr = def.mParameterUnits;

          Identification::NewbaseParameterDef nbParamDef;
          if (Identification::gridDef.getNewbaseParameterDefByFmiId(def.mFmiParameterId,nbParamDef)  &&  !nbParamDef.mParameterName.empty())
          {
            pName = pName + " (" + nbParamDef.mParameterName + ")";
          }
        }


        if (parameterIdStr.empty())
          parameterIdStr = pId;

        if (parameterIdStr == pId)
        {
          ostr << "<OPTION selected value=\"" <<  pId << "\">" <<  pName << "</OPTION>\n";
        }
        else
        {
          ostr << "<OPTION value=\"" <<  pId << "\">" <<  pName << "</OPTION>\n";
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### Level identifiers:

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByParameterAndGenerationId(0,gid,T::ParamKeyType::FMI_NAME,parameterIdStr,T::ParamLevelIdType::IGNORE,0,0,0,-2,-2,-2,"10000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    T::ParamLevelId levelId = (T::ParamLevelId)atoi(parameterLevelIdStr.c_str());

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Level type:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    std::set<int> levelIds;
    getLevelIds(contentInfoList,levelIds);

    if (levelIds.find(levelId) == levelIds.end())
      levelId = 0;

    if (levelIds.size() > 0)
    {
      std::string disabled = "";
      if (levelIds.size() == 1)
        disabled = "disabled";

      ostr << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it = levelIds.begin(); it != levelIds.end(); ++it)
      {
        if (parameterLevelIdStr.empty())
        {
          parameterLevelIdStr = std::to_string(*it);
          levelId = *it;
        }

        std::string lStr = std::to_string(*it);
        Identification::LevelDef levelDef;
        if (Identification::gridDef.getFmiLevelDef(*it,levelDef))
          lStr = std::to_string(*it) + " " + levelDef.mDescription;

        if (levelId == *it)
          ostr << "<OPTION selected value=\"" <<  *it << "\">" <<  lStr << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  *it << "\">" <<  lStr << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### Levels:

    contentInfoList.clear();
    contentServer->getContentListByParameterAndGenerationId(0,gid,T::ParamKeyType::FMI_NAME,parameterIdStr,T::ParamLevelIdType::FMI,levelId,0,0x7FFFFFFF,-2,-2,-2,"10000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    T::ParamLevel level = (T::ParamLevel)atoi(parameterLevelStr.c_str());

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Level:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    std::set<int> levels;
    getLevels(contentInfoList,levelId,levels);

    if (levels.find(level) == levels.end())
      level = 0;

    if (levels.size() > 0)
    {
      std::string disabled = "";
      if (levels.size() == 1)
        disabled = "disabled";

      ostr << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it = levels.begin(); it != levels.end(); ++it)
      {
        if (parameterLevelStr.empty())
        {
          parameterLevelStr = std::to_string(*it);
          level = *it;
        }

        if (level == *it)
          ostr << "<OPTION selected value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### forecastTypeStr:

    short forecastType = (short)atoi(forecastTypeStr.c_str());

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Forecast type and number</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    std::set<int> forecastTypes;
    getForecastTypes(contentInfoList,levelId,level,forecastTypes);

    if (forecastTypes.find(forecastType) == forecastTypes.end())
      forecastType = 0;

    if (forecastTypes.size() > 0)
    {
      std::string disabled = "";
      if (forecastTypes.size() == 1)
        disabled = "disabled";

      ostr << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&forecastType=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it = forecastTypes.begin(); it != forecastTypes.end(); ++it)
      {
        if (forecastTypeStr.empty())
        {
          forecastTypeStr = std::to_string(*it);
          forecastType = *it;
        }

        if (forecastType == *it)
          ostr << "<OPTION selected value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  *it << "\">" << *it << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
    }
    //ostr << "</TD></TR>\n";


    // ### forecastNumber:

    short forecastNumber = (short)atoi(forecastNumberStr.c_str());
    //int prevNumber = -100;

    //ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Forecast number:</TD></TR>\n";
    //ostr << "<TR height=\"30\"><TD>\n";

    std::set<int> forecastNumbers;
    getForecastNumbers(contentInfoList,levelId,level,forecastType,forecastNumbers);

    if (forecastNumbers.find(forecastNumber) == forecastNumbers.end())
      forecastNumber = 0;

    if (forecastNumbers.size() > 0)
    {
      std::string disabled = "";
      if (forecastNumbers.size() == 1)
        disabled = "disabled";

      ostr << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&forecastType=" + forecastTypeStr + "&forecastNumber=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it = forecastNumbers.begin(); it != forecastNumbers.end(); ++it)
      {
        if (forecastNumberStr.empty())
        {
          forecastNumberStr = std::to_string(*it);
          forecastNumber = *it;
        }

        if (*it == forecastNumber)
          ostr << "<OPTION selected value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  *it << "\">" <<  *it << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";

    // ### Geometries:

    T::GeometryId geometryId  = (T::GeometryId)atoi(geometryIdStr.c_str());

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Geometry:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    std::set<int> geometries;
    getGeometries(contentInfoList,levelId,level,forecastType,forecastNumber,geometries);

    if (geometries.find(geometryId) == geometries.end())
      geometryId = 0;

    if (geometries.size() > 0)
    {
      std::string disabled = "";
      if (geometries.size() == 1)
        disabled = "disabled";

      ostr << "<SELECT " << disabled << " onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&geometryId=' + this.options[this.selectedIndex].value)\">\n";

      for (auto it=geometries.begin(); it!=geometries.end(); ++it)
      {
        std::string st = std::to_string(*it);

        std::string gName;
        uint cols = 0;
        uint rows = 0;

        Identification::gridDef.getGeometryNameById(*it,gName);

        if (Identification::gridDef.getGridDimensionsByGeometryId(*it,cols,rows))
          st = gName + " (" + std::to_string(cols) + " x " + std::to_string(rows) + ")";
        else
          st = gName;

        if (geometryId == 0)
        {
          geometryId = *it;
          geometryIdStr = std::to_string(geometryId);
        }

        if (geometryId == *it)
          ostr << "<OPTION selected value=\"" <<  *it << "\">" <<  st << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  *it << "\">" <<  st << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### Times:

    contentInfoList.clear();
    contentServer->getContentListByParameterAndGenerationId(0,gid,T::ParamKeyType::FMI_NAME,parameterIdStr,T::ParamLevelIdType::FMI,levelId,level,level,-2,-2,-2,"10000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    std::string prevTime = "19000101T0000";

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Time (UTC):</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD><TABLE><TR><TD>\n";

    //ostr << "<TR height=\"30\"><TD>\n";

    T::ContentInfo *prevCont = NULL;
    T::ContentInfo *currentCont = NULL;
    T::ContentInfo *nextCont = NULL;


    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiName_level_starttime_file_message);

      ostr << "<SELECT id=\"timeselect\" onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&&geometryId=" << geometryIdStr << "' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&geometryId=" << geometryIdStr << "' + this.options[this.selectedIndex].value)\"";

      ostr << " >\n";

      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (g->mGeometryId == geometryId)
        {
          if (prevTime < g->mForecastTime)
          {
            if (forecastType == g->mForecastType)
            {
              if (forecastNumber == g->mForecastNumber)
              {
                std::string url = "&start=" + g->mForecastTime + "&fileId=" + std::to_string(g->mFileId) + "&messageIndex=" + std::to_string(g->mMessageIndex) + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr;

                if (currentCont != NULL  &&  nextCont == NULL)
                  nextCont = g;

                if (startTime.empty())
                  startTime = g->mForecastTime;

                if (startTime == g->mForecastTime)
                {
                  currentCont = g;
                  ostr << "<OPTION selected value=\"" <<  url << "\">" <<  g->mForecastTime << "</OPTION>\n";
                  fileIdStr = std::to_string(g->mFileId);
                  messageIndexStr = std::to_string(g->mMessageIndex);
                }
                else
                {
                  ostr << "<OPTION value=\"" <<  url << "\">" <<  g->mForecastTime << "</OPTION>\n";
                }

                if (currentCont == NULL)
                  prevCont = g;

                prevTime = g->mForecastTime;
              }
            }
          }
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD>\n";
    //ostr << "</TD></TR>\n";

    //ostr << "<TR><TD><TABLE height=\"30\"><TR>\n";

    if (prevCont != NULL)
      ostr << "<TD width=\"20\" > <button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + prevCont->mForecastTime + "&fileId=" + std::to_string(prevCont->mFileId) + "&messageIndex=" + std::to_string(prevCont->mMessageIndex) + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "');\">&lt;</button></TD>\n";
    else
      ostr << "<TD width=\"20\"><button type=\"button\">&lt;</button></TD></TD>\n";

    if (nextCont != NULL)
      ostr << "<TD width=\"20\"><button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + nextCont->mForecastTime + "&fileId=" + std::to_string(nextCont->mFileId) + "&messageIndex=" + std::to_string(nextCont->mMessageIndex) + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "');\">&gt;</button></TD>\n";
    else
      ostr << "<TD width=\"20\"><button type=\"button\">&gt;</button></TD></TD>\n";

    ostr << "</TR></TABLE></TD></TR>\n";

    // ### Modes:


    const char *modes[] = {"image","map","info",/*"table(full)",*/"table(sample)",/*"coordinates(full)",*/"coordinates(sample)",NULL};

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Presentation:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";
    ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&presentation=' + this.options[this.selectedIndex].value)\">\n";

    uint a = 0;
    while (modes[a] != NULL)
    {
      if (presentation.empty())
        presentation = modes[a];

      if (presentation == modes[a])
        ostr << "<OPTION selected value=\"" <<  modes[a] << "\">" <<  modes[a] << "</OPTION>\n";
      else
        ostr << "<OPTION value=\"" <<  modes[a] << "\">" <<  modes[a] << "</OPTION>\n";

      a++;
    }
    ostr << "</SELECT>\n";
    ostr << "</TD></TR>\n";



    if (presentation == "image" || presentation == "map")
    {
      ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Hue, saturation and blur</TD></TR>\n";
      ostr << "<TR height=\"30\"><TD>\n";
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&hue=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&seaMask=" + seaMaskStr + "&hue=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&hue=' + this.options[this.selectedIndex].value)\"";

      ostr << " >\n";


      uint hue = (uint)atoi(hueStr.c_str());
      for (uint a=0; a<256; a++)
      {
        if (a == hue)
          ostr << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
      //ostr << "</TD></TR>\n";



      //ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Saturation:</TD></TR>\n";
      //ostr << "<TR height=\"30\"><TD>\n";
      uint saturation = (uint)atoi(saturationStr.c_str());
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&geometryId=" + geometryIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&seaMask=" + seaMaskStr + "&landMask=" + landMaskStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      ostr << " >\n";


      for (uint a=0; a<256; a++)
      {
        if (a == saturation)
          ostr << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
      //ostr << "</TD></TR>\n";



      //ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Blur:</TD></TR>\n";
      //ostr << "<TR height=\"30\"><TD>\n";
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&coordinateLines=" + coordinateLinesStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&coordinateLines=" + coordinateLinesStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&coordinateLines=" + coordinateLinesStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      ostr << " >\n";


      uint blur = (uint)atoi(blurStr.c_str());
      for (uint a=1; a<=200; a++)
      {
        if (a == blur)
          ostr << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
      ostr << "</TD></TR>\n";



      ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Coordinate lines:</TD></TR>\n";
      ostr << "<TR height=\"30\"><TD>\n";
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&landBorder=" + landBorderStr + "&coordinateLines=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&landBorder=" + landBorderStr + "&coordinateLines=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&landBorder=" + landBorderStr + "&coordinateLines=' + this.options[this.selectedIndex].value)\"";

      ostr << " >\n";

      uint a = 0;
      while (colorNames[a] != NULL)
      {
        if (coordinateLinesStr == colorNames[a])
          ostr << "<OPTION selected value=\"" << colorNames[a] << "\">" <<  colorNames[a] << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  colorNames[a] << "\">" <<  colorNames[a] << "</OPTION>\n";

        a++;
      }
      ostr << "</SELECT>\n";
      ostr << "</TD></TR>\n";


      ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Land border:</TD></TR>\n";
      ostr << "<TR height=\"30\"><TD>\n";
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&landBorder=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&landBorder=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landMask=" + landMaskStr + "&seaMask=" + seaMaskStr + "&landBorder=' + this.options[this.selectedIndex].value)\"";

      ostr << " >\n";

      a = 0;
      while (colorNames[a] != NULL)
      {
        if (landBorderStr == colorNames[a])
          ostr << "<OPTION selected value=\"" << colorNames[a] << "\">" <<  colorNames[a] << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  colorNames[a] << "\">" <<  colorNames[a] << "</OPTION>\n";

        a++;
      }
      ostr << "</SELECT>\n";
      ostr << "</TD></TR>\n";
    }

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Land and sea masks:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";
    ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&seaMask=" + seaMaskStr + "&landMask=' + this.options[this.selectedIndex].value)\"";

    if (presentation == "image")
      ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&seaMask=" + seaMaskStr + "&landMask=' + this.options[this.selectedIndex].value)\"";

    if (presentation == "map")
      ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&seaMask=" + seaMaskStr + "&landMask=' + this.options[this.selectedIndex].value)\"";

    ostr << " >\n";

    const char *landMasks[] = {"simple",NULL};
    a = 0;
    while (landMasks[a] != NULL)
    {
      if (landMaskStr == landMasks[a])
        ostr << "<OPTION selected value=\"" << landMasks[a] << "\">" <<  landMasks[a] << "</OPTION>\n";
      else
        ostr << "<OPTION value=\"" <<  landMasks[a] << "\">" <<  landMasks[a] << "</OPTION>\n";

      a++;
    }
    a = 0;
    while (colorNames[a] != NULL)
    {
      if (landMaskStr == colorNames[a])
        ostr << "<OPTION selected value=\"" << colorNames[a] << "\">" <<  colorNames[a] << "</OPTION>\n";
      else
        ostr << "<OPTION value=\"" <<  colorNames[a] << "\">" <<  colorNames[a] << "</OPTION>\n";

      a++;
    }
    ostr << "</SELECT>\n";

    //ostr << "</TD></TR>\n";

    //ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Sea mask:</TD></TR>\n";
    //ostr << "<TR height=\"30\"><TD>\n";
    ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=' + this.options[this.selectedIndex].value)\"";

    if (presentation == "image")
      ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=' + this.options[this.selectedIndex].value)\"";

    if (presentation == "map")
      ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&coordinateLines=" + coordinateLinesStr + "&landBorder=" + landBorderStr + "&landMask=" + landMaskStr + "&seaMask=' + this.options[this.selectedIndex].value)\"";

    ostr << " >\n";

    const char *seaMasks[] = {"simple",NULL};
    a = 0;
    while (seaMasks[a] != NULL)
    {
      if (seaMaskStr == seaMasks[a])
        ostr << "<OPTION selected value=\"" << seaMasks[a] << "\">" <<  seaMasks[a] << "</OPTION>\n";
      else
        ostr << "<OPTION value=\"" <<  seaMasks[a] << "\">" <<  seaMasks[a] << "</OPTION>\n";

      a++;
    }
    a = 0;
    while (colorNames[a] != NULL)
    {
      if (seaMaskStr == colorNames[a])
        ostr << "<OPTION selected value=\"" << colorNames[a] << "\">" <<  colorNames[a] << "</OPTION>\n";
      else
        ostr << "<OPTION value=\"" <<  colorNames[a] << "\">" <<  colorNames[a] << "</OPTION>\n";

      a++;
    }

    ostr << "</SELECT>\n";
    ostr << "</TD></TR>\n";



    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Units:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD><INPUT type=\"text\" value=\"" << unitStr << "\"></TD></TR>\n";

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Value:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD><INPUT type=\"text\" id=\"gridValue\"></TD></TR>\n";
/*
    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Timeseries:</TD></TR>\n";
    ostr << "<TR height=\"100\"><TD><IMG id=\"timeseries\" style=\"height:100; max-width:250;\" alt=\"\" src=\"/grid-gui?page=timeseries&fileId=0&messageIndex=" << messageIndexStr << "\"></TD></TR>\n";
*/
    ostr << "<TR height=\"50%\"><TD></TD></TR>\n";
    ostr << "</TABLE>\n";
    ostr << "</TD>\n";

    //ostr << "<TD align=\"center\" valign=\"center\">\n";
    //ostr << "<TABLE width=\"100%\" height=\"100%\"><TR>\n";
    if (presentation == "image")
    {
      ostr << "<TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:100%;\" src=\"/grid-gui?page=image&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "&geometryId=" << geometryIdStr << "&hue=" << hueStr << "&saturation=" << saturationStr << "&blur=" << blurStr <<  "&coordinateLines=" << coordinateLinesStr << "&landBorder=" << landBorderStr << "&landMask=" << landMaskStr <<  "&seaMask=" << seaMaskStr <<  "\" onclick=\"getImageCoords(event,this," << fileIdStr << "," << messageIndexStr << ",'" << presentation << "');\"></TD>";
    }
    else
    if (presentation == "map")
    {
      ostr << "<TD><IMG id=\"myimage\" style=\"background:#000000; max-width:100%; height:100%;\" src=\"/grid-gui?page=map&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "&hue=" << hueStr << "&saturation=" << saturationStr << "&blur=" << blurStr << "&coordinateLines=" << coordinateLinesStr << "&landBorder=" << landBorderStr << "&landMask=" << landMaskStr << "&seaMask=" << seaMaskStr <<  "\"></TD>";
    }
    else
    if (presentation == "table(sample)" /* || presentation == "table(full)"*/)
    {
      ostr << "<TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?page=table&presentation=" + presentation + "&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "\">";
      ostr << "<p>Your browser does not support iframes.</p>\n";
      ostr << "</IFRAME></TD>";
    }
    else
    if (presentation == "coordinates(sample)" /* || presentation == "coordinates(full)"*/)
    {
      ostr << "<TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?page=coordinates&presentation=" + presentation + "&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "\">";
      ostr << "<p>Your browser does not support iframes.</p>\n";
      ostr << "</IFRAME></TD>";
    }
    else
    if (presentation == "info")
    {
      ostr << "<TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?page=info&presentation=" + presentation + "&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "\">";
      ostr << "<p>Your browser does not support iframes.</p>\n";
      ostr << "</IFRAME></TD>";
    }

    ostr << "</TR>\n";

    Identification::FmiParameterDef pDef;
    if (Identification::gridDef.getFmiParameterDefByName(parameterIdStr,pDef))
      ostr << "<TR><TD style=\"height:25; vertical-align:middle; text-align:left; font-size:12;\">" << pDef.mParameterDescription << "</TD></TR>\n";

    //ostr << "</TABLE>\n";
    //ostr << "</TD></TR>\n";


    ostr << "</TABLE>\n";
    ostr << "</BODY></HTML>\n";

    theResponse.setContent(std::string(ostr.str()));
    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





bool Plugin::request(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    std::string page = "main";
    boost::optional<std::string> v = theRequest.getParameter("page");
    if (v)
      page = *v;

    if (page == "main")
      return page_main(theReactor,theRequest,theResponse);

    if (page == "image")
      return page_image(theReactor,theRequest,theResponse);

    if (page == "map")
      return page_map(theReactor,theRequest,theResponse);

    if (page == "info")
      return page_info(theReactor,theRequest,theResponse);

    if (page == "table")
      return page_table(theReactor,theRequest,theResponse);

    if (page == "coordinates")
      return page_coordinates(theReactor,theRequest,theResponse);

    if (page == "value")
      return page_value(theReactor,theRequest,theResponse);

    if (page == "timeseries")
      return page_timeseries(theReactor,theRequest,theResponse);

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}





// ----------------------------------------------------------------------
/*!
 * \brief Main request handler
 */
// ----------------------------------------------------------------------

void Plugin::requestHandler(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    try
    {
      // We return JSON, hence we should enable CORS
      theResponse.setHeader("Access-Control-Allow-Origin", "*");

      const int expires_seconds = 1;
      boost::posix_time::ptime t_now = boost::posix_time::second_clock::universal_time();

      bool response = request(theReactor, theRequest, theResponse);

      if (response)
      {
        theResponse.setStatus(HTTP::Status::ok);
      }
      else
      {
        theResponse.setStatus(HTTP::Status::not_implemented);
      }

      // Adding response headers

      boost::posix_time::ptime t_expires = t_now + boost::posix_time::seconds(expires_seconds);
      boost::shared_ptr<Fmi::TimeFormatter> tformat(Fmi::TimeFormatter::create("http"));
      std::string cachecontrol =
          "public, max-age=" + boost::lexical_cast<std::string>(expires_seconds);
      std::string expiration = tformat->format(t_expires);
      std::string modification = tformat->format(t_now);

      theResponse.setHeader("Cache-Control", cachecontrol.c_str());
      theResponse.setHeader("Expires", expiration.c_str());
      theResponse.setHeader("Last-Modified", modification.c_str());
    }
    catch (...)
    {
      // Catching all exceptions

      SmartMet::Spine::Exception exception(BCP, "Request processing exception!", NULL);
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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

bool Plugin::queryIsFast(const SmartMet::Spine::HTTP::Request & /* theRequest */) const
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
