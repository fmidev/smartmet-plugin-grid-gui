#include "ColorMapFile.h"
#include <grid-files/common/GeneralFunctions.h>
#include <grid-files/common/AutoThreadLock.h>


namespace SmartMet
{
namespace T
{



ColorMapFile::ColorMapFile()
{
  try
  {
    mLastModified = 0;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





ColorMapFile::ColorMapFile(const std::string& filename)
{
  try
  {
    mFilename = filename;
    mLastModified = 0;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





ColorMapFile::ColorMapFile(const ColorMapFile& colorMapFile)
{
  try
  {
    mNames = colorMapFile.mNames;
    mFilename = colorMapFile.mFilename;
    mColorMap = colorMapFile.mColorMap;
    mLastModified = colorMapFile.mLastModified;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





ColorMapFile::~ColorMapFile()
{
  try
  {
  }
  catch (...)
  {
    SmartMet::Spine::Exception exception(BCP,"Destructor failed",nullptr);
    exception.printError();
  }
}




void ColorMapFile::init()
{
  try
  {
    AutoThreadLock lock(&mThreadLock);
    loadFile();
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





void ColorMapFile::init(const std::string& filename)
{
  try
  {
    mFilename = filename;
    init();
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





bool ColorMapFile::checkUpdates()
{
  try
  {
    AutoThreadLock lock(&mThreadLock);

    time_t tt = getFileModificationTime(mFilename.c_str());

    if (tt != mLastModified  &&  (tt+3) < time(nullptr))
    {
      loadFile();
      return true;
    }
    return false;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





void ColorMapFile::getValuesAndColors(std::vector<float>& values,std::vector<unsigned int>& colors)
{
  try
  {
    AutoThreadLock lock(&mThreadLock);

    for (auto it = mColorMap.begin(); it != mColorMap.end(); ++it)
    {
      values.push_back(it->first);
      colors.push_back(it->second);
    }
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





uint ColorMapFile::getColor(double value)
{
  try
  {
    AutoThreadLock lock(&mThreadLock);

    auto it = mColorMap.find(value);
    if (it != mColorMap.end())
      return it->second;

    it = mColorMap.lower_bound(value);
    if (it != mColorMap.end())
    {
      if (it->first > value  && it != mColorMap.begin())
        it--;

      return it->second;
    }

    if (value > mColorMap.rbegin()->first)
      return mColorMap.rbegin()->second;

    if (value < mColorMap.begin()->first)
      return mColorMap.begin()->second;

    return 0xFFFFFFFF;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





time_t ColorMapFile::getLastModificationTime()
{
  try
  {
    return mLastModified;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





std::string ColorMapFile::getFilename()
{
  try
  {
    return mFilename;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





string_vec ColorMapFile::getNames()
{
  try
  {
    return mNames;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





bool ColorMapFile::hasName(const char *name)
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
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





void ColorMapFile::print(std::ostream& stream,uint level,uint optionFlags)
{
  try
  {
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





void ColorMapFile::loadFile()
{
  try
  {
    FILE *file = fopen(mFilename.c_str(),"re");
    if (file == nullptr)
    {
      SmartMet::Spine::Exception exception(BCP,"Cannot open file!");
      exception.addParameter("Filename",mFilename);
      throw exception;
    }

    mColorMap.clear();
    mNames.clear();

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

          if ((*p == ';' || *p == ','  || *p == '\n') && !ind)
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
            if (strcasecmp(field[0],"NAME") == 0)
            {
              mNames.push_back(std::string(field[1]));
            }
            else
            {
              double val = toDouble(field[0]);
              uint color = 0;
              if (c > 3)
                color = (atoll(field[1]) << 16) + (atoll(field[2]) << 8) + atoll(field[3]);
              else
                color = strtoul(field[1],nullptr,16);

              mColorMap.insert(std::pair<double,unsigned int>(val,color));
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
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}




}
}
