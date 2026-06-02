#pragma once

#include "ColorMapFile.h"
#include <spine/SmartMetPlugin.h>
#include <spine/Reactor.h>
#include <spine/HTTP.h>
#include <engines/grid/Engine.h>
#include <grid-files/common/ImageFunctions.h>
#include <grid-files/common/BitLine.h>
#include <grid-files/common/Session.h>


namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{

typedef std::vector<std::pair<std::string,unsigned int>> Colors;  //!< Named color list: (name, packed ARGB) pairs loaded from the color definition CSV.

/*! \brief All visual parameters that control how a single grid field is rendered to an image.
 *
 *  Passed to saveImage() so that rendering decisions (color map, stream lines, land/sea
 *  shading, coordinate lines) are bundled together rather than spread across many arguments. */
struct ImagePaintParameters
{
  std::string imageFile;                        //!< Output image file path.
  T::FileId fileId = 0;                         //!< Grid file identifier.
  T::MessageIndex messageIndex = 0;             //!< Message index within the grid file.
  T::GeometryId geometryId = 0;                 //!< Source geometry identifier.
  T::GeometryId projectionId = 0;               //!< Target projection geometry identifier.
  bool zeroIsMissing = false;                   //!< Treat zero values as missing/transparent.
  unsigned char paint_alpha = 255;              //!< Overall image opacity (0=transparent, 255=opaque).
  std::string   paint_colorMapName;             //!< Name of the color map to use for value-to-color mapping.
  unsigned char paint_hue = 0;                  //!< Hue shift applied to the color-mapped output.
  unsigned char paint_saturation = 0;           //!< Saturation level for the color-mapped output.
  unsigned char paint_blur = 1;                 //!< Blur radius applied after color mapping.
  int  stream_step = 10;                        //!< Grid spacing (cells) between streamline seed points.
  int  stream_minLength = 6;                    //!< Minimum streamline length (pixels) to draw.
  int  stream_maxLength = 64;                   //!< Maximum streamline length (pixels) to draw.
  uint stream_color = 0x808080;                 //!< Packed ARGB color used to draw streamlines.
  bool stream_animation = false;                //!< Produce an animated WebP sequence for streamlines.
  uint landBorder_color = 0xFF808080;           //!< Packed ARGB color for land border lines.
  uint landColor = 0xFF808080;                  //!< Packed ARGB base color for land areas.
  uint landColor_position = 1;                  //!< Z-order position for the land color layer.
  uint landShading_light = 128;                 //!< Light intensity for land topographic shading.
  uint landShading_shadow = 160;                //!< Shadow intensity for land topographic shading.
  uint landShading_position = 2;                //!< Z-order position for the land shading layer.
  uint seaColor = 0xFF0000FF;                   //!< Packed ARGB base color for sea areas.
  uint seaColor_position = 1;                   //!< Z-order position for the sea color layer.
  uint seaShading_light = 128;                  //!< Light intensity for sea depth shading.
  uint seaShading_shadow = 160;                 //!< Shadow intensity for sea depth shading.
  uint seaShading_position = 2;                 //!< Z-order position for the sea shading layer.
  uint coordinateLine_color = 0xFFC0C0C0;       //!< Packed ARGB color for latitude/longitude grid lines.
};




// ====================================================================================
/*! \brief SmartMet Server plugin that provides browser-based visualization of
 *  meteorological grid data (GRIB1/GRIB2/NetCDF/QueryData).
 *
 *  Registered at `/grid-gui` as an admin-only handler.  Dispatches on the `page`
 *  session parameter to: an HTML form UI (main), color-mapped raster images (image),
 *  streamline/vector field animations (streams, streamsAnimation), geographic map
 *  overlays (map), metadata display (info), raw message display (message), file
 *  downloads (download), tabular data (table), coordinate lookups (coordinates), and
 *  point value queries (value).  All page state is carried in a Session object. */
// ====================================================================================

class Plugin : public SmartMetPlugin
{
  public:

    Plugin() = delete;
    Plugin(const Plugin& other) = delete;
    Plugin& operator=(const Plugin& other) = delete;
    Plugin(Spine::Reactor* theReactor, const char* theConfig);
    virtual ~Plugin();

    const std::string& getPluginName() const;
    int getRequiredAPIVersion() const;
    bool isAdminQuery(const Spine::HTTP::Request& theRequest) const;

  protected:

    void init();
    void shutdown();
    void requestHandler(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

  private:

    int request(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    int page_info(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_message(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_download(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_value(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_table(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_coordinates(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_image(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_streams(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_streamsAnimation(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    /*! \brief Shared implementation for the two streams-rendering page handlers.
     *  When animation is false: emits a PNG of stream lines.
     *  When animation is true: emits an animated WebP of moving stream points. */
    int page_streamsImpl(const Spine::HTTP::Request& theRequest,
                         Spine::HTTP::Response& theResponse,
                         Session& session,
                         bool animation);

    /*! \brief Emit the JavaScript helper block used by the page_main HTML page (mouse
     *  hover handlers, page navigation, image swap, XHR helper for grid-value lookup,
     *  etc.).  Writes directly to the given output stream. */
    void page_main_writeJavascript(std::ostringstream& output);

    /*! \brief Scan a parameter-value vector and compute the minimum, maximum and a
     *  per-class step size used by saveMap()'s implicit colour scaling when no explicit
     *  colour map file is configured.  The step is clamped so that an asymmetric
     *  distribution (max >> avg) doesn't compress most of the range into one bucket. */
    void computeValueRangeForMap(const T::ParamValue_vec& values,
                                 double& minValue,
                                 double& maxValue,
                                 double& step);

    int page_map(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_main(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);


    void saveImage(ImagePaintParameters& params,int width,int height,
                      T::ParamValue_vec& values,
                      T::Coordinate_vec& coordinates,
                      T::Coordinate_vec *lineCoordinates);

    void saveImage(ImagePaintParameters& params);

    void saveMap(const char *imageFile,
                      uint columns,
                      uint rows,
                      T::ParamValue_vec& values,
                      unsigned char hue,
                      unsigned char saturation,
                      unsigned char blur,
                      uint coordinateLines,
                      uint landBorder,
                      std::string landMask,
                      std::string seaMask,
                      std::string colorMapName,
                      std::string missingStr);


    T::ColorMapFile*  getColorMapFile(std::string colorMapName);

    void checkImageCache();
    void getGenerations(T::GenerationInfoList& generationInfoList,std::set<std::string>& generations);
    void getLevelIds(T::ContentInfoList& contentInfoList,std::set<int>& levelIds);
    void getLevels(T::ContentInfoList& contentInfoList,int levelId,std::set<int>& levels);
    std::string getFmiKey(std::string& producerName,T::ContentInfo& contentInfo);
    void getForecastTypes(T::ContentInfoList& contentInfoList,int levelId,int level,std::set<int>& forecastTypes);
    void getForecastNumbers(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,std::set<int>& forecastNumbers);
    void getGeometries(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,int forecastNumber,std::set<int>& geometries);
    uint getColorValue(std::string& colorName);
    void initSession(Session& session);
    void loadColorFile();
    void loadProducerFile();
    bool loadImage(const char *fname,Spine::HTTP::Response &theResponse);

  private:

    const std::string         itsModuleName;                    //!< Plugin name returned by getPluginName().
    Spine::Reactor*           itsReactor;                       //!< Back-pointer to the server reactor (not owned).
    ConfigurationFile         itsConfigurationFile;             //!< Parsed libconfig configuration.
    std::string               itsGridConfigFile;                //!< Path to the grid-files library config file.
    string_vec                itsColorMapFileNames;             //!< Paths to color map CSV files configured for this plugin.
    T::ColorMapFile_vec       itsColorMapFiles;                 //!< Loaded and hot-reloadable color map files.
    std::string               itsColorFile;                     //!< Path to the named color definitions CSV.
    Colors                    itsColors;                        //!< Named colors loaded from itsColorFile.
    time_t                    itsColors_lastModified;           //!< Last-modified time of itsColorFile; used to detect reload need.
    std::string               itsImageCache_dir;                //!< Directory where rendered image files are cached.
    uint                      itsImageCache_maxImages;          //!< Maximum number of images to keep in the cache before pruning.
    uint                      itsImageCache_minImages;          //!< Minimum number of images to retain after a prune pass.
    bool                      itsAnimationEnabled;              //!< Whether WebP animation rendering is enabled.
    std::string               itsImagesUnderConstruction[100];  //!< Slot array tracking image files currently being rendered (prevents duplicate work).
    uint                      itsImageCounter;                  //!< Rolling counter used to generate unique image file names.
    ThreadLock                itsThreadLock;                    //!< Lock serialising concurrent access to image cache state.
    std::string               itsProducerFile;                  //!< Path to the producer filter file listing visible producers.
    time_t                    itsProducerFile_modificationTime; //!< Last-modified time of itsProducerFile; used to detect reload need.
    std::set<std::string>     itsProducerList;                  //!< Set of producer names loaded from itsProducerFile (empty = show all).
    std::set<int>             itsBlockedProjections;            //!< Geometry IDs excluded from the projection selector (too large to display).

    std::shared_ptr<Engine::Grid::Engine> itsGridEngine;        //!< Grid engine used for content and data server access.
    std::map<std::string,std::string>      itsImages;           //!< Maps image hash keys to cached image file paths.
};  // class Plugin

}  // namespace GridGui
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
