#pragma once

#include <grid-files/common/ModificationLock.h>
#include <grid-files/common/Typedefs.h>
#include <map>
#include <vector>


namespace SmartMet
{
namespace T
{

typedef std::map<float,unsigned int> ColorMap;  //!< Maps parameter value thresholds (float) to packed ARGB colors (uint).


// ====================================================================================
/*! \brief Manages a CSV-based color mapping file that translates grid parameter values
 *  to packed ARGB colors for image rendering.
 *
 *  A single file can define several named color maps (e.g. one per parameter or
 *  physical scale).  getColor() does exact threshold lookup; getSmoothColor() linearly
 *  interpolates between adjacent thresholds.  The file is hot-reloaded on modification
 *  so that color schemes can be adjusted without restarting the server. */
// ====================================================================================

class ColorMapFile
{
  public:
                      ColorMapFile();
                      ColorMapFile(const std::string& filename);
                      ColorMapFile(const ColorMapFile& colorMapFile);
    virtual           ~ColorMapFile();

    void              init();
    void              init(const std::string& filename);
    bool              checkUpdates();
    uint              getColor(double value);
    uint              getSmoothColor(double value);
    void              getValuesAndColors(std::vector<float>& values,std::vector<unsigned int>& colors);
    std::string       getFilename();
    time_t            getLastModificationTime();
    string_vec        getNames();
    ModificationLock* getModificationLock();

    bool              hasName(const char *name);

    void              print(std::ostream& stream,uint level,uint optionFlags);

  protected:

    void              loadFile();

    string_vec        mNames;           //!< Named color maps defined in this file (e.g. "Dali Temperature (Celsius)").
    std::string       mFilename;        //!< Path to the CSV color map file on disk.
    ColorMap          mColorMap;        //!< Loaded threshold → color mapping for the active color map.
    time_t            mLastModified;    //!< Last-modified time of the file; used to detect when a reload is needed.
    ModificationLock  mModificationLock;//!< Lock protecting concurrent access to mColorMap during hot-reload.
};


typedef std::vector<ColorMapFile> ColorMapFile_vec;  //!< Collection of loaded color map files.


}  // namespace T
}  // namespace SmartMet
