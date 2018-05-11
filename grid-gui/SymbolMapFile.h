#pragma once

#include <grid-files/common/ThreadLock.h>
#include <grid-files/common/Typedefs.h>
#include <grid-files/common/ImageFunctions.h>
#include <map>
#include <vector>


namespace SmartMet
{
namespace T
{

typedef std::map<double,std::string> SymbolMap;
typedef std::map<std::string,CImage> SymbolCache;


class SymbolMapFile
{
  public:
                    SymbolMapFile();
                    SymbolMapFile(std::string filename);
                    SymbolMapFile(const SymbolMapFile& symbolMapFile);
    virtual         ~SymbolMapFile();

    void            init();
    void            init(std::string filename);
    bool            checkUpdates();
    bool            getSymbol(double value,CImage& symbol);
    string_vec      getNames();
    bool            hasName(const char *name);

    void            print(std::ostream& stream,uint level,uint optionFlags);

  protected:

    void            loadFile();

    string_vec      mNames;
    std::string     mFilename;
    std::string     mDir;
    SymbolMap       mSymbolMap;
    SymbolCache     mSymbolCache;
    time_t          mLastModified;
    ThreadLock      mThreadLock;
};


typedef std::vector<SymbolMapFile> SymbolMapFile_vec;


}  // namespace T
}  // namespace SmartMet
