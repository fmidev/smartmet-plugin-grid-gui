#pragma once

#include <vector>
#include <string>


namespace SmartMet
{
namespace T
{


class Location
{
  public:
                        Location();
                        Location(const Location& location);
                        Location(const char *name,double x,double y);
    virtual             ~Location();

    void                print(std::ostream& stream,uint level,uint optionFlags);

    std::string         mName;
    double              mX;
    double              mY;
};


typedef std::vector<Location> Location_vec;


}  // namespace T
}  // namespace SmartMet
