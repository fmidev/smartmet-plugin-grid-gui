// ======================================================================
/*!
 * \brief SmartMet Grid Gui plugin implementation
 */
// ======================================================================

#include "Plugin.h"
#include "grid-files/common/GeneralFunctions.h"
#include "grid-files/common/ImageFunctions.h"
#include "grid-files/identification/GribDef.h"
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


const char *levelDefinition[] =
{
  "0",
  "1 Gound or water surface",
  "2 Pressure level",
  "3 Hybrid level",
  "4 Altitude",
  "5 Top of atmosphere",
  "6 Height above ground in meters",
  "7 Mean sea level",
  "8 Entire atmosphere",
  "9 Depth below land surface",
  "10 Depth below some surface",
  "11 Level at specified pressure difference from ground to level",
  "12 Max equivalent potential temperature level",
  "13 Layer between two metric heights above ground",
  "14 Layer between two depths below land surface",
  "15 Isothermal level, temperature in 1/100 K"
};


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
    itsReactor = theReactor;

    if (theReactor->getRequiredAPIVersion() != SMARTMET_API_VERSION)
      throw SmartMet::Spine::Exception(BCP, "GridGui plugin and Server API version mismatch");

    // Register the handler
    if (!theReactor->addContentHandler(
            this, "/grid-gui", boost::bind(&Plugin::callRequestHandler, this, _1, _2, _3)))
      throw SmartMet::Spine::Exception(BCP, "Failed to register GridGui request handler");

    itsConfig.readFile(theConfig);

    if (!itsConfig.exists("grid.configDirectory"))
      throw SmartMet::Spine::Exception(BCP, "The 'grid.configDirectory' attribute not specified in the config file");

    itsConfig.lookupValue("grid.configDirectory", itsGridConfigDirectory);

    Identification::gribDef.init(itsGridConfigDirectory.c_str());
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





void Plugin::saveMap(const char *imageFile,uint columns,uint rows,T::ParamValue_vec&  values,unsigned char hue,unsigned char saturation,unsigned char blur)
{
  try
  {
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

    uint xx = columns / 36;
    uint yy = rows / 18;

    double dd = maxValue - minValue;
    double step = dd / 200;

    int width = columns;
    int height = rows;

    unsigned long *image = new unsigned long[width*height];

    //unsigned char hue = 30;
    //unsigned char saturation = 128;
    uint c = 0;

    for (int y=0; y<height; y++)
    {
      for (int x=0; x<width; x++)
      {
        T::ParamValue val = values[c];
        //printf("Val(%u,%u) : %f\n",x,y,val);
        uint v = 200 - (uint)((val - minValue) / step);
        v = v / blur;
        v = v * blur;
        v = v + 55;
        uint col = hsv_to_rgb(hue,saturation,(unsigned char)v);
        if (val == ParamValueMissing)
          col = 0xE8E8E8;

        if ((x % xx) == 0  ||  (y % yy) == 0)
          col = 0xFFFFFF;

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




void Plugin::saveImage(const char *imageFile,T::GridData&  gridData,unsigned char hue,unsigned char saturation,unsigned char blur,bool rotate)
{
  try
  {
    double maxValue = -1000000000;
    double minValue = 1000000000;

    int width = gridData.mColumns;
    int height = gridData.mRows;

    uint size = width*height;
    std::size_t sz = gridData.mValues.size();

    if (sz != (uint)size)
    {
      printf("ERROR: There are not enough values (= %u) for the grid (%u x %u)!\n",(uint)sz,width,height);
      return;
    }

    for (std::size_t t=0; t<sz; t++)
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


    unsigned long *image = new unsigned long[size];
    //unsigned char hue = 250; // 30;
    //unsigned char saturation = 84; // = 128;
    uint c = 0;

    if (!rotate)
    {
      for (int y=0; y<height; y++)
      {
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

          image[y*width + x] = col;
          c++;
        }
      }
    }
    else
    {
      for (int y=height-1; y>=0; y--)
      {
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

          image[y*width + x] = col;
          c++;
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
    std::shared_ptr<SmartMet::ContentServer::ServiceInterface> contentServer = itsGridEngine->getContentServerPtr();
    std::shared_ptr<SmartMet::DataServer::ServiceInterface>  dataServer = itsGridEngine->getDataServerPtr();

    std::string fileIdStr = "";
    std::string messageIndexStr = "0";

    boost::optional<std::string> v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      std::string messageIndexStr = *v;

    if (fileIdStr.length() == 0)
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
    result = dataServer->getGridAttributeList(0,contentInfo.mFileId,contentInfo.mMessageIndex,attributeList);
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

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">File</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Id</TD><TD>" << fileInfo.mFileId << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Name</TD><TD>" << fileInfo.mName << "</TD></TR>\n";

    ostr << "</TABLE></TD></TR>\n";



    ostr << "<TR><TD bgColor=\"#000080\" width=\"100\">Parameter</TD><TD><TABLE border=\"1\" width=\"100%\" style=\"font-size:12;\">\n";

    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Message index</TD><TD>" << contentInfo.mMessageIndex << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Start time</TD><TD>" << contentInfo.mForecastTime << "</TD></TR>\n";
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
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Type Of Ensemble Forecast</TD><TD>" << (int)contentInfo.mForecastType << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\"> Number</TD><TD>" << (int)contentInfo.mForecastNumber << "</TD></TR>\n";
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
    //std::shared_ptr<SmartMet::ContentServer::ServiceInterface> contentServer = itsGridEngine->getContentServerPtr();
    std::shared_ptr<SmartMet::DataServer::ServiceInterface>  dataServer = itsGridEngine->getDataServerPtr();
    //std::shared_ptr<SmartMet::QueryServer::ServiceInterface> queryServer = itsGridEngine->getQueryServerPt();

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
      std::string messageIndexStr = *v;


    if (fileIdStr.length() == 0)
      return true;

    std::ostringstream ostr;

    T::GridData gridData;
    int result = dataServer->getGridData(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),gridData);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridData()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }

    T::GridCoordinates coordinates;
    result = dataServer->getGridCoordinates(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),T::CoordinateType::ORIGINAL_COORDINATES,coordinates);
    if (result != 0)
    {
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridCoordinates()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }


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
      sprintf(tmp,"%.3f",coordinates.mCoordinateList[x].x());
      ostr << "<TD>" << tmp << "</TD>";
    }
    ostr << "</TR>\n";


    // ### Rows:

    for (uint y=0; y<height; y++)
    {
      c = y*gridData.mColumns;

      // ### Row index and Y coordinate:

      sprintf(tmp,"%.3f",coordinates.mCoordinateList[c].y());
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





bool Plugin::page_value(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    std::shared_ptr<SmartMet::ContentServer::ServiceInterface> contentServer = itsGridEngine->getContentServerPtr();
    std::shared_ptr<SmartMet::DataServer::ServiceInterface>  dataServer = itsGridEngine->getDataServerPtr();
    //std::shared_ptr<SmartMet::QueryServer::ServiceInterface> queryServer = itsGridEngine->getQueryServerPt();

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

    GRIB2::GridDefinition_ptr def =  Identification::gribDef.getGridDefinition2ByGeometryId(contentInfo.mGeometryId);
    if (def == NULL)
      return true;

    T::Dimensions_opt dim = def->getGridDimensions();
    if (!dim)
      return true;

    uint height = dim->ny();
    uint width = dim->nx();

    double xx = (double)(xPos * (double)width);
    double yy = (double)(yPos * (double)height);

    if (presentation == "image(rotated)")
      yy = height-yy;

    T::ParamValue value;
    dataServer->getGridValue(0,fileId,messageIndex,T::CoordinateType::GRID_COORDINATES,xx,yy,T::InterpolationMethod::Linear,value);

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
    std::shared_ptr<SmartMet::ContentServer::ServiceInterface> contentServer = itsGridEngine->getContentServerPtr();
    std::shared_ptr<SmartMet::DataServer::ServiceInterface>  dataServer = itsGridEngine->getDataServerPtr();
    //std::shared_ptr<SmartMet::QueryServer::ServiceInterface> queryServer = itsGridEngine->getQueryServerPt();

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

    GRIB2::GridDefinition_ptr def =  Identification::gribDef.getGridDefinition2ByGeometryId(contentInfo.mGeometryId);
    if (def == NULL)
      return true;

    T::Dimensions_opt dim = def->getGridDimensions();
    if (!dim)
      return true;

    uint height = dim->ny();
    uint width = dim->nx();

    double xx = (double)(xPos * (double)width);
    double yy = (double)(yPos * (double)height);

    if (presentation == "image(rotated)")
      yy = height-yy;

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByParameterAndGenerationId(0,contentInfo.mGenerationId,T::ParamKeyType::FMI_ID,contentInfo.mFmiParameterId,T::ParamLevelIdType::FMI,contentInfo.mFmiParameterLevelId,contentInfo.mParameterLevel,contentInfo.mParameterLevel,"19000101T000000","23000101T000000",0,contentInfoList);

    contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiId_level_starttime_file_message);

    std::vector <T::ParamValue> valueList;

    int idx = -1;
    //std::ostringstream ostr;
    std::set<int> dayIdx;

    uint c = 0;
    uint len = contentInfoList.getLength();
    for (uint t=0; t<len; t++)
    {
      T::ContentInfo *info = contentInfoList.getContentInfoByIndex(t);

      if (info->mGeometryId == contentInfo.mGeometryId  &&  info->mForecastType == contentInfo.mForecastType  &&  info->mForecastNumber == contentInfo.mForecastNumber)
      {
        T::ParamValue value;
        if (dataServer->getGridValue(0,info->mFileId,info->mMessageIndex,T::CoordinateType::GRID_COORDINATES,xx,yy,T::InterpolationMethod::Linear,value) == 0)
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
    //std::shared_ptr<SmartMet::ContentServer::ServiceInterface> contentServer = itsGridEngine->getContentServerPtr();
    std::shared_ptr<SmartMet::DataServer::ServiceInterface>  dataServer = itsGridEngine->getDataServerPtr();
    //std::shared_ptr<SmartMet::QueryServer::ServiceInterface> queryServer = itsGridEngine->getQueryServerPt();

    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string hueStr = "110";
    std::string saturationStr = "0";
    bool rotate = false;
    std::string presentation = "image(rotated)";
    std::string blurStr = "1";

    boost::optional<std::string> v;
    v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      std::string messageIndexStr = *v;

    v = theRequest.getParameter("rotate");
    if (v  &&  *v == "no")
       rotate = false;

    if (v  &&  *v == "yes")
       rotate = true;

    v = theRequest.getParameter("hue");
    if (v)
      hueStr = *v;

    v = theRequest.getParameter("saturation");
    if (v)
      saturationStr = *v;

    v = theRequest.getParameter("blur");
    if (v)
      blurStr = *v;

    T::GridData gridData;

    int result = dataServer->getGridData(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),gridData);

    if (result != 0)
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridData()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }


    char fname[200];
    sprintf(fname,"/tmp/image_%llu.jpg",getTime());
    saveImage(fname,gridData,(unsigned char)atoi(hueStr.c_str()),(unsigned char)atoi(saturationStr.c_str()),(unsigned char)atoi(blurStr.c_str()),rotate);

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





bool Plugin::page_map(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    //std::shared_ptr<SmartMet::ContentServer::ServiceInterface> contentServer = itsGridEngine->getContentServerPtr();
    std::shared_ptr<SmartMet::DataServer::ServiceInterface>  dataServer = itsGridEngine->getDataServerPtr();
    //std::shared_ptr<SmartMet::QueryServer::ServiceInterface> queryServer = itsGridEngine->getQueryServerPt();

    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string hueStr = "110";
    std::string saturationStr = "0";
    std::string blurStr = "1";

    boost::optional<std::string> v;
    v = theRequest.getParameter("fileId");
    if (v)
      fileIdStr = *v;

    v = theRequest.getParameter("messageIndex");
    if (v)
      std::string messageIndexStr = *v;


    v = theRequest.getParameter("hue");
    if (v)
      hueStr = *v;

    v = theRequest.getParameter("saturation");
    if (v)
      saturationStr = *v;

    v = theRequest.getParameter("blur");
    if (v)
      blurStr = *v;

    uint columns = 1800;
    uint rows = 900;
    T::ParamValue_vec values;

    int result = dataServer->getGridValuesByArea(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),T::CoordinateType::LATLON_COORDINATES,columns,rows,-180,90,360/(double)columns,-180/(double)rows,T::InterpolationMethod::Nearest,values);
    if (result != 0)
    {
      std::ostringstream ostr;
      ostr << "<HTML><BODY>\n";
      ostr << "DataServer request 'getGridValuesByArea()' failed : " << result << "\n";
      ostr << "</BODY></HTML>\n";
      theResponse.setContent(std::string(ostr.str()));
      return true;
    }


    char fname[200];
    sprintf(fname,"/tmp/image_%llu.jpg",getTime());
    saveMap(fname,columns,rows,values,(unsigned char)atoi(hueStr.c_str()),(unsigned char)atoi(saturationStr.c_str()),(unsigned char)atoi(blurStr.c_str()));

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





bool Plugin::page_main(SmartMet::Spine::Reactor &theReactor,
                            const HTTP::Request &theRequest,
                            HTTP::Response &theResponse)
{
  try
  {
    std::shared_ptr<SmartMet::ContentServer::ServiceInterface> contentServer = itsGridEngine->getContentServerPtr();
    //std::shared_ptr<SmartMet::DataServer::ServiceInterface>  dataServer = itsGridEngine->getDataServerPtr();
    //std::shared_ptr<SmartMet::QueryServer::ServiceInterface> queryServer = itsGridEngine->getQueryServerPt();

    std::string producerIdStr = "";
    std::string generationIdStr = "";
    std::string geometryIdStr = "";
    std::string parameterIdStr = "";
    std::string parameterLevelStr = "";
    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string parameterLevelIdStr;
    std::string hueStr = "110";
    std::string forecastTypeStr = "";
    std::string forecastNumberStr = "";
    std::string saturationStr = "0";
    std::string blurStr = "1";
    std::string startTime = "";
    std::string unitStr = "";
    std::string presentation = "image(rotated)";

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
      std::string messageIndexStr = *v;

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

    ostr << "  var url2 = \"/grid-gui?page=timeseries&presentation=\" + presentation + \"&fileId=\" + fileId + \"&messagIndex=\" + messageIndex + \"&x=\" + prosX + \"&y=\" + prosY;\n";
    //ostr << "  var txt2 = httpGet(url2);\n";
    //ostr << "  document.getElementById('timeseries').value = txt2;\n";
    ostr << "  document.getElementById('timeseries').src = url2;\n";

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
    uint pid = (uint)atoi(producerIdStr.c_str());

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Producer:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=' + this.options[this.selectedIndex].value)\">\n";
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
    len = generationInfoList.getLength();
    uint gid = (uint)atoi(generationIdStr.c_str());

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Generation:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=' + this.options[this.selectedIndex].value)\">\n";


      if (gid == 0)
      {
        T::GenerationInfo *biggest = generationInfoList.getGenerationInfoByIndex(0);
        for (uint a=1; a<len; a++)
        {
          T::GenerationInfo *g = generationInfoList.getGenerationInfoByIndex(a);
          if (g->mName > biggest->mName)
            biggest = g;
        }
        gid = biggest->mGenerationId;
        generationIdStr = std::to_string(gid);
      }

      for (uint a=0; a<len; a++)
      {
        T::GenerationInfo *g = generationInfoList.getGenerationInfoByIndex(a);

        if (gid == g->mGenerationId)
          ostr << "<OPTION selected value=\"" <<  g->mGenerationId << "\">" <<  g->mName << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  g->mGenerationId << "\">" <<  g->mName << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";



    //T::ContentInfoList contentParamList;
    //contentServer->getContentParamListByGenerationId(0,gid,contentParamList);

    //T::ContentInfoList contentInfoList;
    //contentServer->getContentListByGenerationId(0,gid,0,0,1000000,contentInfoList);


    // ### Parameters:

    std::set<std::string> paramKeyList;
    contentServer->getContentParamKeyListByGenerationId(0,gid,T::ParamKeyType::FMI_NAME,paramKeyList);

    //len = contentParamList.getLength();
    //std::string prevFmiName;

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Parameter:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    //printf("ProducerId %u\n",pid);
    //printf("GenerationId %u\n",gid);
    //printf("ContentLen %u\n",len);

    if (paramKeyList.size() > 0)
    {
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=' + this.options[this.selectedIndex].value)\">\n";
      for (auto it=paramKeyList.begin(); it!=paramKeyList.end(); ++it)
      {
        Identification::ParameterDefinition_fmi_cptr def = Identification::gribDef.mMessageIdentifier_fmi.getParameterDefByName(*it);
        std::string pId = *it;
        std::string pName = *it;
        if (def != NULL)
        {
          pId = def->mFmiParameterId;
          pName = def->mParameterName;
          unitStr = def->mParameterUnits;
        }

        if (parameterIdStr.length() == 0)
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
    contentServer->getContentListByParameterAndGenerationId(0,gid,T::ParamKeyType::FMI_ID,parameterIdStr,T::ParamLevelIdType::IGNORE,0,0,0,"10000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    T::ParamLevelId levelId = (T::ParamLevelId)atoi(parameterLevelIdStr.c_str());
    int prevLevelId = -1;

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Level type:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiId_fmiLevelId_level_starttime_file_message);

      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=' + this.options[this.selectedIndex].value)\">\n";
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (parameterLevelIdStr.length() == 0)
        {
          parameterLevelIdStr = std::to_string((int)g->mFmiParameterLevelId);
          levelId = (int)g->mFmiParameterLevelId;
        }

        if (prevLevelId < (int)g->mFmiParameterLevelId)
        {
          std::string lStr = std::to_string((int)g->mFmiParameterLevelId);
          if (g->mFmiParameterLevelId > 0  && (int)g->mFmiParameterLevelId < 16)
            lStr = levelDefinition[(int)g->mFmiParameterLevelId];

          if (levelId == g->mFmiParameterLevelId)
            ostr << "<OPTION selected value=\"" <<  (int)g->mFmiParameterLevelId << "\">" <<  lStr << "</OPTION>\n";
          else
            ostr << "<OPTION value=\"" <<  (int)g->mFmiParameterLevelId << "\">" <<  lStr << "</OPTION>\n";

          prevLevelId = (int)g->mFmiParameterLevelId;
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### Levels:

    contentInfoList.clear();
    contentServer->getContentListByParameterAndGenerationId(0,gid,T::ParamKeyType::FMI_ID,parameterIdStr,T::ParamLevelIdType::FMI,levelId,0,0xFFFFFFFF,"10000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    T::ParamLevel level = (T::ParamLevel)atoi(parameterLevelStr.c_str());
    int prevLevel = -1;

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Level:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiId_level_starttime_file_message);

      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=' + this.options[this.selectedIndex].value)\">\n";
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (prevLevel < (int)g->mParameterLevel)
        {
          if (levelId == g->mFmiParameterLevelId)
          {
            if (parameterLevelStr.length() == 0)
            {
              parameterLevelStr = std::to_string((int)g->mParameterLevel);
              level = g->mParameterLevel;
            }

            if (level == g->mParameterLevel)
              ostr << "<OPTION selected value=\"" <<  (int)g->mParameterLevel << "\">" <<  (int)g->mParameterLevel << "</OPTION>\n";
            else
              ostr << "<OPTION value=\"" <<  (int)g->mParameterLevel << "\">" <<  (int)g->mParameterLevel << "</OPTION>\n";

            prevLevel = (int)g->mParameterLevel;
          }
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### forecastTypeStr:

    short forecastType = (short)atoi(forecastTypeStr.c_str());
    int prevType = -1;

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Forecast type:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiId_fmiLevelId_level_starttime_file_message);

      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&forecastType=' + this.options[this.selectedIndex].value)\">\n";
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (prevType < (int)g->mForecastType)
        {
          if (levelId == g->mFmiParameterLevelId)
          {
            if (level == g->mParameterLevel)
            {
              if (forecastTypeStr.length() == 0)
              {
                forecastTypeStr = std::to_string((int)g->mForecastType);
                forecastType = g->mForecastType;
              }

              if (forecastType == g->mForecastType)
                ostr << "<OPTION selected value=\"" <<  (int)g->mForecastType << "\">" <<  (int)g->mForecastType << "</OPTION>\n";
              else
                ostr << "<OPTION value=\"" <<  (int)g->mForecastType << "\">" <<  (int)g->mForecastType << "</OPTION>\n";

              prevType = (int)g->mForecastType;
            }
          }
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### forecastNumber:

    short forecastNumber = (short)atoi(forecastNumberStr.c_str());
    int prevNumber = -100;

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Forecast number:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&forecastType=" + forecastTypeStr + "&forecastNumber=' + this.options[this.selectedIndex].value)\">\n";
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (prevNumber < (int)g->mForecastNumber)
        {
          if (levelId == g->mFmiParameterLevelId)
          {
            if (level == g->mParameterLevel)
            {
              if (forecastType == g->mForecastType)
              {
                if (forecastNumberStr.length() == 0)
                {
                  forecastNumberStr = std::to_string((int)g->mForecastNumber);
                  forecastNumber = g->mForecastNumber;
                }

                if (forecastNumber == g->mForecastNumber)
                  ostr << "<OPTION selected value=\"" <<  (int)g->mForecastNumber << "\">" <<  (int)g->mForecastNumber << "</OPTION>\n";
                else
                  ostr << "<OPTION value=\"" <<  (int)g->mForecastNumber << "\">" <<  (int)g->mForecastNumber << "</OPTION>\n";

                prevNumber = (int)g->mForecastNumber;
              }
            }
          }
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";

    // ### Geometries:

    uint geometryId  = (uint)atoi(geometryIdStr.c_str());

    std::set<uint> geometryIdList;

    for (uint a=0; a<len; a++)
    {
      T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

      if (gid == g->mGenerationId &&
          parameterIdStr == g->mFmiParameterId &&
          levelId == g->mFmiParameterLevelId  &&
          level == g->mParameterLevel  &&
          forecastType == g->mForecastType &&
          forecastNumber == g->mForecastNumber)
      {
        if (geometryIdList.find(g->mGeometryId) == geometryIdList.end())
          geometryIdList.insert(g->mGeometryId);
      }
    }

    //contentInfoList.getContentGeometryIdListByGenerationId(gid,geometryIdList);

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Geometry:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (geometryIdList.size() > 0)
    {
      //ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=' + this.options[this.selectedIndex].value)\">\n";
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&geometryId=' + this.options[this.selectedIndex].value)\">\n";

      std::set<uint>::iterator it;
      for (it=geometryIdList.begin(); it!=geometryIdList.end(); ++it)
      {
        std::string st = std::to_string(*it);

        GRIB2::GridDefinition_ptr def =  Identification::gribDef.getGridDefinition2ByGeometryId(*it);
        if (def != NULL)
        {
          T::Dimensions_opt d = def->getGridDimensions();

          if (d)
            st = def->getGridGeometryName() + " (" + std::to_string(d->nx()) + " x " + std::to_string(d->ny()) + ")";
          else
            st = def->getGridGeometryName();
        }

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
    contentServer->getContentListByParameterAndGenerationId(0,gid,T::ParamKeyType::FMI_ID,parameterIdStr,T::ParamLevelIdType::FMI,levelId,level,level,"10000101T000000","30000101T000000",0,contentInfoList);
    len = contentInfoList.getLength();
    std::string prevTime = "19000101T0000";

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Time:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD><TABLE><TR><TD>\n";

    //ostr << "<TR height=\"30\"><TD>\n";

    T::ContentInfo *prevCont = NULL;
    T::ContentInfo *currentCont = NULL;
    T::ContentInfo *nextCont = NULL;


    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiId_level_starttime_file_message);

      ostr << "<SELECT id=\"timeselect\" onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=no' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image(rotated)")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=yes' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map' + this.options[this.selectedIndex].value)\"";

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
                std::string url = "&start=" + g->mForecastTime + "&fileId=" + std::to_string(g->mFileId) + "&messageIndex=" + std::to_string(g->mMessageIndex) + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr;

                if (currentCont != NULL  &&  nextCont == NULL)
                  nextCont = g;

                if (startTime.length() == 0)
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
      ostr << "<TD width=\"20\" > <button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + prevCont->mForecastTime + "&fileId=" + std::to_string(prevCont->mFileId) + "&messageIndex=" + std::to_string(prevCont->mMessageIndex) + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "');\">&lt;</button></TD>\n";
    else
      ostr << "<TD width=\"20\"><button type=\"button\">&lt;</button></TD></TD>\n";

    if (nextCont != NULL)
      ostr << "<TD width=\"20\"><button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + nextCont->mForecastTime + "&fileId=" + std::to_string(nextCont->mFileId) + "&messageIndex=" + std::to_string(nextCont->mMessageIndex) + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "');\">&gt;</button></TD>\n";
    else
      ostr << "<TD width=\"20\"><button type=\"button\">&gt;</button></TD></TD>\n";

    ostr << "</TR></TABLE></TD></TR>\n";

    // ### Modes:


    const char *modes[] = {"image","image(rotated)","map","info","table(full)","table(sample)"};

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Presentation:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";
    ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&presentation=' + this.options[this.selectedIndex].value)\">\n";

    for (uint a=0; a<6; a++)
    {
      if (presentation.length() == 0)
        presentation = modes[a];

      if (presentation == modes[a])
        ostr << "<OPTION selected value=\"" <<  modes[a] << "\">" <<  modes[a] << "</OPTION>\n";
      else
        ostr << "<OPTION value=\"" <<  modes[a] << "\">" <<  modes[a] << "</OPTION>\n";
    }
    ostr << "</SELECT>\n";
    ostr << "</TD></TR>\n";



    if (presentation == "image" || presentation == "image(rotated)" || presentation == "map")
    {
      ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Hue:</TD></TR>\n";
      ostr << "<TR height=\"30\"><TD>\n";
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&hue=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=no&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&hue=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image(rotated)")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=yes&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&hue=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&hue=' + this.options[this.selectedIndex].value)\"";

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
      ostr << "</TD></TR>\n";



      ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Saturation:</TD></TR>\n";
      ostr << "<TR height=\"30\"><TD>\n";
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&geometryId=" + geometryIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&blur=" + blurStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=no&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&blur=" + blurStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image(rotated)")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=yes&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&blur=" + blurStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&blur=" + blurStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      ostr << " >\n";


      uint saturation = (uint)atoi(saturationStr.c_str());
      for (uint a=0; a<256; a++)
      {
        if (a == saturation)
          ostr << "<OPTION selected value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  a << "\">" <<  a << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
      ostr << "</TD></TR>\n";




      ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Blur:</TD></TR>\n";
      ostr << "<TR height=\"30\"><TD>\n";
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&geometryId=" + geometryIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&forecastType=" + forecastTypeStr + "&forecastNumber=" + forecastNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=no&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image(rotated)")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=yes&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&forecastType=" + forecastTypeStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=' + this.options[this.selectedIndex].value)\"";

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

    }

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Units:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD><INPUT type=\"text\" value=\"" << unitStr << "\"></TD></TR>\n";

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Value:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD><INPUT type=\"text\" id=\"gridValue\"></TD></TR>\n";

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Timeseries:</TD></TR>\n";
    ostr << "<TR height=\"100\"><TD><IMG id=\"timeseries\" style=\"height:100; max-width:250;\" alt=\"\" src=\"/grid-gui?page=timeseries&fileId=0&messageIndex=0\"></TD></TR>\n";

    ostr << "<TR height=\"50%\"><TD></TD></TR>\n";
    ostr << "</TABLE>\n";
    ostr << "</TD>\n";

    //ostr << "<TD align=\"center\" valign=\"center\">\n";
    //ostr << "<TABLE width=\"100%\" height=\"100%\"><TR>\n";
    if (presentation == "image")
    {
      ostr << "<TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:100%;\" src=\"/grid-gui?page=image&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "&rotate=no&hue=" << hueStr << "&saturation=" << saturationStr << "&blur=" << blurStr <<  "\" onclick=\"getImageCoords(event,this," << fileIdStr << "," << messageIndexStr << ",'" << presentation << "');\"></TD>";
    }
    else
    if (presentation == "image(rotated)")
    {
      ostr << "<TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:100%;\" src=\"/grid-gui?page=image&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "&rotate=yes&hue=" << hueStr << "&saturation=" << saturationStr << "&blur=" << blurStr <<  "\" onclick=\"getImageCoords(event,this," << fileIdStr << "," << messageIndexStr << ",'" << presentation << "');\"></TD>";
    }
    else
    if (presentation == "map")
    {
      ostr << "<TD><IMG id=\"myimage\" style=\"background:#000000; max-width:100%; height:100%;\" src=\"/grid-gui?page=map&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "&hue=" << hueStr << "&saturation=" << saturationStr << "&blur=" << blurStr <<  "\"></TD>";
    }
    else
    if (presentation == "table(sample)"  || presentation == "table(full)")
    {
      ostr << "<TD><IFRAME width=\"100%\" height=\"100%\" src=\"grid-gui?page=table&presentation=" + presentation + "&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "\">";
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

    Identification::ParameterDefinition_fmi_cptr pDef = Identification::gribDef.mMessageIdentifier_fmi.getParameterDefById(parameterIdStr);
    if (pDef != NULL)
      ostr << "<TR><TD style=\"height:25; vertical-align:middle; text-align:left; font-size:12;\">" << pDef->mParameterDescription << "</TD></TR>\n";

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
