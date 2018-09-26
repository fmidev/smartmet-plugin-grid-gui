#pragma once

#include "Location.h"
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
                        LocationFile(const std::string& filename);
                        LocationFile(const LocationFile& locationFile);
    virtual             ~LocationFile();

    void                init();
    void                init(const std::string& filename);
    bool                checkUpdates();
    T::Coordinate_vec   getCoordinates();
    std::string         getFilename();
    T::Location_vec&    getLocations();
    time_t              getLastModificationTime();
    string_vec          getNames();
    bool                hasName(const char *name);

    void                print(std::ostream& stream,uint level,uint optionFlags);

  protected:

    void                loadFile();

    string_vec          mNames;
    std::string         mFilename;
    T::Location_vec     mLocations;
    time_t              mLastModified;
    ThreadLock          mThreadLock;
};


typedef std::vector<LocationFile> LocationFile_vec;


}  // namespace T
}  // namespace SmartMet
