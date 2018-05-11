#include "LocationFile.h"
#include <grid-files/common/GeneralFunctions.h>
#include <grid-files/common/AutoThreadLock.h>


namespace SmartMet
{
namespace T
{



LocationFile::LocationFile()
{
  try
  {
    mLastModified = 0;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





LocationFile::LocationFile(std::string filename)
{
  try
  {
    mFilename = filename;
    mLastModified = 0;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





LocationFile::LocationFile(const LocationFile& colorMapFile)
{
  try
  {
    mNames = colorMapFile.mNames;
    mFilename = colorMapFile.mFilename;
    mCoordinates = colorMapFile.mCoordinates;
    mLastModified = colorMapFile.mLastModified;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





LocationFile::~LocationFile()
{
  try
  {
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}




void LocationFile::init()
{
  try
  {
    AutoThreadLock lock(&mThreadLock);
    loadFile();
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





void LocationFile::init(std::string filename)
{
  try
  {
    mFilename = filename;
    init();
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





bool LocationFile::checkUpdates()
{
  try
  {
    AutoThreadLock lock(&mThreadLock);

    time_t tt = getFileModificationTime(mFilename.c_str());

    if (tt != mLastModified  &&  (tt+3) < time(0))
    {
      loadFile();
      return true;
    }
    return false;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





T::Coordinate_vec LocationFile::getCoordinates()
{
  try
  {
    return mCoordinates;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





string_vec LocationFile::getNames()
{
  try
  {
    return mNames;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





bool LocationFile::hasName(const char *name)
{
  try
  {
    for (auto it = mNames.begin(); it != mNames.end(); ++it)
    {
      if (strcasecmp(name,it->c_str()) == 0)
        return true;
    }
    return false;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





void LocationFile::print(std::ostream& stream,uint level,uint optionFlags)
{
  try
  {
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}





void LocationFile::loadFile()
{
  try
  {
    FILE *file = fopen(mFilename.c_str(),"r");
    if (file == NULL)
    {
      SmartMet::Spine::Exception exception(BCP,"Cannot open file!");
      exception.addParameter("Filename",mFilename);
      throw exception;
    }

    mCoordinates.clear();
    mNames.clear();

    char st[1000];

    while (!feof(file))
    {
      if (fgets(st,1000,file) != NULL  &&  st[0] != '#')
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
          if (strcasecmp(field[0],"NAME") == 0)
          {
            mNames.push_back(std::string(field[1]));
          }
          else
          {
            if (field[0][0] != '\0' &&  field[1][0] != '\0')
            {
              double lat = atof(field[0]);
              double lon = atof(field[1]);
              mCoordinates.push_back(T::Coordinate(lon,lat));
            }
          }
        }
      }
    }
    fclose(file);

    mLastModified = getFileModificationTime(mFilename.c_str());
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", NULL);
  }
}




}
}
