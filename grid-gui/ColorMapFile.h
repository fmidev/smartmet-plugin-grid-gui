#pragma once

#include <grid-files/common/ThreadLock.h>
#include <grid-files/common/Typedefs.h>
#include <map>
#include <vector>


namespace SmartMet
{
namespace T
{

typedef std::map<double,unsigned int> ColorMap;


class ColorMapFile
{
  public:
                    ColorMapFile();
                    ColorMapFile(std::string filename);
                    ColorMapFile(const ColorMapFile& colorMapFile);
    virtual         ~ColorMapFile();

    void            init();
    void            init(std::string filename);
    bool            checkUpdates();
    uint            getColor(double value);
    string_vec      getMapNames();
    bool            hasName(const char *name);

    void            print(std::ostream& stream,uint level,uint optionFlags);

  protected:

    void            loadFile();

    string_vec      mMapNames;
    std::string     mFilename;
    ColorMap        mColorMap;
    time_t          mLastModified;
    ThreadLock      mThreadLock;
};


typedef std::vector<ColorMapFile> ColorMapFile_vec;


}  // namespace T
}  // namespace SmartMet