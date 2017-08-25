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
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Start time</TD><TD>" << contentInfo.mStartTime << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">End time</TD><TD>" << contentInfo.mEndTime << "</TD></TR>\n";
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
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Type Of Ensemble Forecast</TD><TD>" << (uint)contentInfo.mTypeOfEnsembleForecast << "</TD></TR>\n";
    ostr << "<TR><TD width=\"180\" bgColor=\"#E0E0E0\">Perturbation Number</TD><TD>" << (uint)contentInfo.mPerturbationNumber << "</TD></TR>\n";

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
    result = dataServer->getGridCoordinates(0,atoi(fileIdStr.c_str()),atoi(messageIndexStr.c_str()),coordinates);
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
          sprintf(tmp,"%.3f",gridData.mValues[c]);
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
    std::string saturationStr = "123";
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

    size_t sz = getFileSize(fname);
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
    std::string saturationStr = "123";
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

    size_t sz = getFileSize(fname);
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
    std::string parameterIdStr = "";
    std::string parameterLevelStr = "";
    std::string fileIdStr = "";
    std::string messageIndexStr = "0";
    std::string parameterLevelIdStr;
    std::string hueStr = "110";
    std::string typeOfEnsembleForecastStr = "";
    std::string perturbationNumberStr = "";
    std::string saturationStr = "123";
    std::string blurStr = "1";
    std::string startTime = "";
    //std::string endTime = "";
    std::string presentation = "image(rotated)";

    boost::optional<std::string> v;
    v = theRequest.getParameter("producerId");
    if (v)
      producerIdStr = *v;

    v = theRequest.getParameter("generationId");
    if (v)
      generationIdStr = *v;

    v = theRequest.getParameter("parameterId");
    if (v)
      parameterIdStr = *v;

    v = theRequest.getParameter("levelId");
    if (v)
      parameterLevelIdStr = *v;

    v = theRequest.getParameter("typeOfEnsembleForecast");
    if (v)
      typeOfEnsembleForecastStr = *v;

    v = theRequest.getParameter("perturbationNumber");
    if (v)
      perturbationNumberStr = *v;

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
      for (uint a=0; a<len; a++)
      {
        T::GenerationInfo *g = generationInfoList.getGenerationInfoByIndex(a);

        if (gid == 0  && a == (len-1))
        {
          gid = g->mGenerationId;
          generationIdStr = std::to_string(gid);
        }

        if (gid == g->mGenerationId)
          ostr << "<OPTION selected value=\"" <<  g->mGenerationId << "\">" <<  g->mName << "</OPTION>\n";
        else
          ostr << "<OPTION value=\"" <<  g->mGenerationId << "\">" <<  g->mName << "</OPTION>\n";
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### Parameters:

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByGenerationId(0,gid,0,0,1000000,contentInfoList);

    len = contentInfoList.getLength();
    std::string prevFmiName;

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Parameter:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    //printf("ProducerId %u\n",pid);
    //printf("GenerationId %u\n",gid);
    //printf("ContentLen %u\n",len);

    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiName_level_starttime_file_message);

      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=' + this.options[this.selectedIndex].value)\">\n";
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (g->mFmiParameterName.length() > 0  &&  g->mFmiParameterName != prevFmiName)
        {
          if (parameterIdStr.length() == 0)
            parameterIdStr = g->mFmiParameterId;


          if (parameterIdStr == g->mFmiParameterId)
            ostr << "<OPTION selected value=\"" <<  g->mFmiParameterId << "\">" <<  g->mFmiParameterName << "</OPTION>\n";
          else
            ostr << "<OPTION value=\"" <<  g->mFmiParameterId << "\">" <<  g->mFmiParameterName << "</OPTION>\n";

          prevFmiName = g->mFmiParameterName;
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### Level identifiers:

    contentInfoList.clear();
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


    // ### typeOfEnsembleForecastStr:

    unsigned char typeOfEnsembleForecast = (unsigned char)atoi(typeOfEnsembleForecastStr.c_str());
    int prevType = -1;

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>TypeOfEnsembleForecast:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiId_fmiLevelId_level_starttime_file_message);

      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&typeOfEnsembleForecast=' + this.options[this.selectedIndex].value)\">\n";
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (prevType < (int)g->mTypeOfEnsembleForecast)
        {
          if (levelId == g->mFmiParameterLevelId)
          {
            if (level == g->mParameterLevel)
            {
              if (typeOfEnsembleForecastStr.length() == 0)
              {
                typeOfEnsembleForecastStr = std::to_string((int)g->mTypeOfEnsembleForecast);
                typeOfEnsembleForecast = g->mTypeOfEnsembleForecast;
              }

              if (typeOfEnsembleForecast == g->mTypeOfEnsembleForecast)
                ostr << "<OPTION selected value=\"" <<  (int)g->mTypeOfEnsembleForecast << "\">" <<  (int)g->mTypeOfEnsembleForecast << "</OPTION>\n";
              else
                ostr << "<OPTION value=\"" <<  (int)g->mTypeOfEnsembleForecast << "\">" <<  (int)g->mTypeOfEnsembleForecast << "</OPTION>\n";

              prevType = (int)g->mTypeOfEnsembleForecast;
            }
          }
        }
      }
      ostr << "</SELECT>\n";
    }
    ostr << "</TD></TR>\n";


    // ### perturbationNumber:

    unsigned char perturbationNumber = (unsigned char)atoi(perturbationNumberStr.c_str());
    int prevNumber = -1;

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>PerturbationNumber:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";

    if (len > 0)
    {
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" << presentation << "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&perturbationNumber=' + this.options[this.selectedIndex].value)\">\n";
      for (uint a=0; a<len; a++)
      {
        T::ContentInfo *g = contentInfoList.getContentInfoByIndex(a);

        if (prevNumber < (int)g->mPerturbationNumber)
        {
          if (levelId == g->mFmiParameterLevelId)
          {
            if (level == g->mParameterLevel)
            {
              if (typeOfEnsembleForecast == g->mTypeOfEnsembleForecast)
              {
                if (perturbationNumberStr.length() == 0)
                {
                  perturbationNumberStr = std::to_string((int)g->mPerturbationNumber);
                  perturbationNumber = g->mPerturbationNumber;
                }

                if (perturbationNumber == g->mPerturbationNumber)
                  ostr << "<OPTION selected value=\"" <<  (int)g->mPerturbationNumber << "\">" <<  (int)g->mPerturbationNumber << "</OPTION>\n";
                else
                  ostr << "<OPTION value=\"" <<  (int)g->mPerturbationNumber << "\">" <<  (int)g->mPerturbationNumber << "</OPTION>\n";

                prevNumber = (int)g->mPerturbationNumber;
              }
            }
          }
        }
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

      ostr << "<SELECT id=\"timeselect\" onchange=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "' + this.options[this.selectedIndex].value)\"";

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

        if (prevTime < g->mStartTime)
        {
          if (typeOfEnsembleForecast == g->mTypeOfEnsembleForecast)
          {
            if (perturbationNumber == g->mPerturbationNumber)
            {
              std::string url = "&start=" + g->mStartTime + "&fileId=" + std::to_string(g->mFileId) + "&messageIndex=" + std::to_string(g->mMessageIndex) + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&perturbationNumber=" + perturbationNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr;

              if (currentCont != NULL  &&  nextCont == NULL)
                nextCont = g;

              if (startTime.length() == 0)
                startTime = g->mStartTime;

              if (startTime == g->mStartTime)
              {
                currentCont = g;
                ostr << "<OPTION selected value=\"" <<  url << "\">" <<  g->mStartTime << "</OPTION>\n";
                fileIdStr = std::to_string(g->mFileId);
                messageIndexStr = std::to_string(g->mMessageIndex);
              }
              else
              {
                ostr << "<OPTION value=\"" <<  url << "\">" <<  g->mStartTime << "</OPTION>\n";
              }

              if (currentCont == NULL)
                prevCont = g;

              prevTime = g->mStartTime;
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
      ostr << "<TD width=\"20\" > <button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + prevCont->mStartTime + "&fileId=" + std::to_string(prevCont->mFileId) + "&messageIndex=" + std::to_string(prevCont->mMessageIndex) + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&perturbationNumber=" + perturbationNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "');\">&lt;</button></TD>\n";
    else
      ostr << "<TD width=\"20\"><button type=\"button\">&lt;</button></TD></TD>\n";

    if (nextCont != NULL)
      ostr << "<TD width=\"20\"><button type=\"button\" onClick=\"getPage(this,parent,'/grid-gui?page=main&presentation=" + presentation + "&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + nextCont->mStartTime + "&fileId=" + std::to_string(nextCont->mFileId) + "&messageIndex=" + std::to_string(nextCont->mMessageIndex) + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&perturbationNumber=" + perturbationNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "');\">&gt;</button></TD>\n";
    else
      ostr << "<TD width=\"20\"><button type=\"button\">&gt;</button></TD></TD>\n";

    ostr << "</TR></TABLE></TD></TR>\n";

    // ### Modes:


    const char *modes[] = {"image","image(rotated)","map","info","table(full)","table(sample)"};

    ostr << "<TR height=\"15\" style=\"font-size:12;\"><TD>Presentation:</TD></TR>\n";
    ostr << "<TR height=\"30\"><TD>\n";
    ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&perturbationNumber=" + perturbationNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&presentation=' + this.options[this.selectedIndex].value)\">\n";

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
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&perturbationNumber=" + perturbationNumberStr + "&saturation=" + saturationStr + "&blur=" + blurStr + "&hue=' + this.options[this.selectedIndex].value)\"";

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
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&perturbationNumber=" + perturbationNumberStr + "&hue=" + hueStr + "&blur=" + blurStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=no&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&hue=" + hueStr + "&blur=" + blurStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image(rotated)")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=yes&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&hue=" + hueStr + "&blur=" + blurStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&hue=" + hueStr + "&blur=" + blurStr + "&saturation=' + this.options[this.selectedIndex].value)\"";

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
      ostr << "<SELECT onchange=\"getPage(this,parent,'/grid-gui?page=main&producerId=" + producerIdStr + "&generationId=" + generationIdStr + "&parameterId=" + parameterIdStr + "&levelId=" + parameterLevelIdStr + "&level=" + parameterLevelStr + "&start=" + startTime + "&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&presentation=" + presentation + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&perturbationNumber=" + perturbationNumberStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=no&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "image(rotated)")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=image&rotate=yes&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=' + this.options[this.selectedIndex].value)\"";

      if (presentation == "map")
        ostr << " onkeydown=\"setImage(document.getElementById('myimage'),'/grid-gui?page=map&fileId=" + fileIdStr + "&messageIndex=" + messageIndexStr + "&typeOfEnsembleForecast=" + typeOfEnsembleForecastStr + "&hue=" + hueStr + "&saturation=" + saturationStr + "&blur=' + this.options[this.selectedIndex].value)\"";

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

    ostr << "<TR height=\"50%\"><TD></TD></TR>\n";
    ostr << "</TABLE>\n";
    ostr << "</TD>\n";

    //ostr << "<TD align=\"center\" valign=\"center\">\n";
    //ostr << "<TABLE width=\"100%\" height=\"100%\"><TR>\n";
    if (presentation == "image")
    {
      ostr << "<TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:100%;\" src=\"/grid-gui?page=image&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "&rotate=no&hue=" << hueStr << "&saturation=" << saturationStr << "&blur=" << blurStr <<  "\"></TD>";
    }
    else
    if (presentation == "image(rotated)")
    {
      ostr << "<TD><IMG id=\"myimage\" style=\"background:#000000; max-width:1800; height:100%; max-height:100%;\" src=\"/grid-gui?page=image&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "&rotate=yes&hue=" << hueStr << "&saturation=" << saturationStr << "&blur=" << blurStr <<  "\"></TD>";
    }
    else
    if (presentation == "map")
    {
      ostr << "<TD><IMG id=\"myimage\" style=\"background:#000000; max-width:100%; height:100%;\" src=\"/grid-gui?page=map&fileId=" << fileIdStr << "&messageIndex=" << messageIndexStr << "&hue=" << hueStr << "&saturation=" << saturationStr << "&blur=" << blurStr << "\"></TD>";
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
