#include "Location.h"
#include <grid-files/common/GeneralFunctions.h>


namespace SmartMet
{
namespace T
{



Location::Location()
{
  try
  {
    mX = 0;
    mY = 0;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Constructor failed!", nullptr);
  }
}





Location::Location(const char *name,double x,double y)
{
  try
  {
    mName = name;
    mX = x;
    mY = y;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Constructor failed!", nullptr);
  }
}





Location::Location(const Location& location)
{
  try
  {
    mName = location.mName;
    mX = location.mX;
    mY = location.mY;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Constructor failed!", nullptr);
  }
}





Location::~Location()
{
  try
  {
  }
  catch (...)
  {
    Fmi::Exception exception(BCP,"Destructor failed",nullptr);
    exception.printError();
  }
}




void Location::print(std::ostream& stream,uint level,uint optionFlags)
{
  try
  {
    stream << space(level) << "Location\n";
    stream << space(level) << "- mName  = " << mName << "\n";
    stream << space(level) << "- mX     = " << mX << "\n";
    stream << space(level) << "- mY     = " << mY << "\n";
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Constructor failed!", nullptr);
  }
}







}
}
