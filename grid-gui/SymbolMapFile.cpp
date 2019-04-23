#include "SymbolMapFile.h"
#include <grid-files/common/GeneralFunctions.h>
#include <grid-files/common/AutoThreadLock.h>


namespace SmartMet
{
namespace T
{



SymbolMapFile::SymbolMapFile()
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





SymbolMapFile::SymbolMapFile(const std::string& filename)
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





SymbolMapFile::SymbolMapFile(const SymbolMapFile& symbolMapFile)
{
  try
  {
    mNames = symbolMapFile.mNames;
    mFilename = symbolMapFile.mFilename;
    mDir = symbolMapFile.mDir;
    mSymbolMap = symbolMapFile.mSymbolMap;
    mLastModified = symbolMapFile.mLastModified;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





SymbolMapFile::~SymbolMapFile()
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





void SymbolMapFile::init()
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





void SymbolMapFile::init(const std::string& filename)
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





bool SymbolMapFile::checkUpdates()
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





time_t SymbolMapFile::getLastModificationTime()
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





std::string SymbolMapFile::getFilename()
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





bool SymbolMapFile::getSymbol(double value,CImage& symbol)
{
  try
  {
    AutoThreadLock lock(&mThreadLock);

    std::string filename = "";

    auto it = mSymbolMap.find(value);
    if (it != mSymbolMap.end())
      filename = it->second;

    if (filename.empty())
    {
      it = mSymbolMap.upper_bound(value);
      if (it != mSymbolMap.end())
        filename = it->second;

      if (filename.empty())
      {
        if (value > mSymbolMap.rbegin()->first)
          filename = mSymbolMap.rbegin()->second;
      }

      if (filename.empty())
      {
        if (value < mSymbolMap.begin()->first)
          filename = mSymbolMap.begin()->second;
      }
    }

    if (!filename.empty())
    {
      std::string fname = mDir + "/" + filename;
/*
      auto it = mSymbolCache.find(fname);
      if (it != mSymbolCache.end())
      {
        symbol = it->second;
        return true;
      }
*/
      if (png_load(fname.c_str(),symbol) == 0)
      {
        mSymbolCache.insert(std::pair<std::string,CImage>(fname,symbol));
        return true;
      }
    }

    return false;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





string_vec SymbolMapFile::getNames()
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





bool SymbolMapFile::hasName(const char *name)
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





void SymbolMapFile::print(std::ostream& stream,uint level,uint optionFlags)
{
  try
  {
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Constructor failed!", nullptr);
  }
}





void SymbolMapFile::loadFile()
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

    mSymbolMap.clear();
    mNames.clear();
    mSymbolCache.clear();

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
            if (strcasecmp(field[0],"NAME") == 0)
            {
              mNames.push_back(std::string(field[1]));
            }
            else
            if (strcasecmp(field[0],"DIR") == 0)
            {
              mDir = field[1];
            }
            else
            {
              double val = toDouble(field[0]);
              std::string filename = field[1];
              mSymbolMap.insert(std::pair<double,std::string>(val,filename));
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
