#pragma once

#include <grid-files/common/ThreadLock.h>
#include <grid-files/common/Typedefs.h>
#include <grid-files/common/Coordinate.h>
#include <map>
#include <vector>


namespace SmartMet
{
namespace T
{


class LocationFile
{
  public:
                        LocationFile();
                        LocationFile(std::string filename);
                        LocationFile(const LocationFile& locationFile);
    virtual             ~LocationFile();

    void                init();
    void                init(std::string filename);
    bool                checkUpdates();
    T::Coordinate_vec   getCoordinates();
    string_vec          getNames();
    bool                hasName(const char *name);

    void                print(std::ostream& stream,uint level,uint optionFlags);

  protected:

    void                loadFile();

    string_vec          mNames;
    std::string         mFilename;
    T::Coordinate_vec   mCoordinates;
    time_t              mLastModified;
    ThreadLock          mThreadLock;
};


typedef std::vector<LocationFile> LocationFile_vec;


}  // namespace T
}  // namespace SmartMet
