#pragma once

#include "ColorMapFile.h"
#include <spine/SmartMetPlugin.h>
#include <spine/Reactor.h>
#include <spine/HTTP.h>
#include <engines/grid/Engine.h>
#include <grid-files/common/ImageFunctions.h>
#include <grid-files/common/BitLine.h>
#include <grid-files/common/Session.h>
#include <json/json.h>
#include <mutex>


namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{

using Colors = std::vector<std::pair<std::string,unsigned int>>;  //!< Named color list: (name, packed ARGB) pairs loaded from the color definition CSV.

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
  T::FileId fileId2 = 0;                        //!< Second field of a difference image (fileId2 - fileId), 0 = no difference.
  T::MessageIndex messageIndex2 = 0;            //!< Message index of the second field of a difference image.
  double differenceRange = 0;                   //!< Value range -r..r of the difference colour scale, 0 = automatic.
  double valueScale = 1;                        //!< Unit conversion of the values: value * valueScale + valueOffset.
  double valueOffset = 0;                       //!< Unit conversion offset; a difference is only scaled.
};



/*! \brief The colour scale of an image without a colour map: values from minValue
 *  upwards in steps of step, the lowest values brightest (see hsvValueImage()). */
struct HsvScale
{
  double minValue = 0;
  double maxValue = 0;
  double avgValue = 0;
  double step = 0;
  uint count = 0;          //!< Number of values that are not missing.
};

HsvScale hsvScale(const T::ParamValue_vec& values);

/*! \brief The automatic range r of the -r..r colour scale of a difference image: the
 *  99th percentile of the absolute differences, so that a few outliers do not wash
 *  out the image. */
double differenceRange(const T::ParamValue_vec& values);

/*! \brief The ARGB colour of a difference in the -range..range scale. */
uint differenceColor(double value,double range);



/*! \brief A lat/lon area of a map image: the longitude of the left edge, the latitude of
 *  the top edge and the size of a pixel in degrees. */
struct MapArea
{
  double lon0 = -180;
  double lat1 = 90;
  double dlon = 0.2;
  double dlat = 0.2;
};



/*! \brief A file of the new user interface, served from memory and reloaded when the file changes. */
struct UiFile
{
  time_t modificationTime = 0;
  std::string content;
  std::string etag;
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

struct PageMainState;

class Plugin : public SmartMetPlugin
{
  public:

    Plugin() = delete;
    Plugin(const Plugin& other) = delete;
    Plugin& operator=(const Plugin& other) = delete;
    Plugin(Spine::Reactor* theReactor, const char* theConfig);
    ~Plugin() override;

    const std::string& getPluginName() const override;
    int getRequiredAPIVersion() const override;
    bool isAdminQuery(const Spine::HTTP::Request& theRequest) const override;

  protected:

    void init() override;
    void shutdown() override;
    void requestHandler(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse) override;

  private:

    int request(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    int page_info(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_message(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_download(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_value(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_table(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_coordinates(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_image(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_streams(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_streamsAnimation(const Spine::Reactor& theReactor,
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
    void page_main_timeOption(PageMainState& ps,Session& session,T::ContentInfo *g,const std::string& u,uint& daySwitch,uint& cc);
    void page_main_collectTimeGroups(PageMainState& ps);
    void page_main_timeGroups(PageMainState& ps,Session& session);
    void page_main_timeSelection(PageMainState& ps,Session& session);
    void page_main_levelBar(PageMainState& ps,Session& session);
    void page_main_hueOptions(PageMainState& ps,Session& session);
    void page_main_streamOptions(PageMainState& ps,Session& session);
    void page_main_coordinateLineOptions(PageMainState& ps,Session& session);
    void page_main_landBorderOptions(PageMainState& ps,Session& session);
    void page_main_landSeaColorOptions(PageMainState& ps,Session& session);
    void page_main_topologyOptions(PageMainState& ps,Session& session);
    void page_main_missingValueOptions(PageMainState& ps,Session& session);
    void page_main_projectionOption(PageMainState& ps,Session& session,int projection,const std::string& st,T::GeometryId& projectionId);
    void page_main_projectionLabel(int projection,std::string& st);
    void page_main_projectionLock(PageMainState& ps,Session& session);
    void page_main_parameterName(PageMainState& ps,const std::string& it,std::string& parameterId,std::string& pName);
    void page_main_generationOption(PageMainState& ps,Session& session,const std::string& name);
    void page_main_selectionAttributes(PageMainState& ps,Session& session);
    void page_main_producers(PageMainState& ps,Session& session);
    void page_main_generations(PageMainState& ps,Session& session);
    void page_main_parameters(PageMainState& ps,Session& session);
    void page_main_levelIds(PageMainState& ps,Session& session);
    void page_main_levels(PageMainState& ps,Session& session);
    void page_main_forecastTypes(PageMainState& ps,Session& session);
    void page_main_forecastNumbers(PageMainState& ps,Session& session);
    void page_main_geometries(PageMainState& ps,Session& session);
    void page_main_times(PageMainState& ps,Session& session);
    void page_main_presentation(PageMainState& ps,Session& session);
    void page_main_projections(PageMainState& ps,Session& session);
    void page_main_colorMaps(PageMainState& ps,Session& session);
    void page_main_paintOptions(PageMainState& ps,Session& session);
    void page_main_imageFrame(PageMainState& ps,Session& session);

    /*! \brief Scan a parameter-value vector and compute the minimum, maximum and a
     *  per-class step size used by saveMap()'s implicit colour scaling when no explicit
     *  colour map file is configured.  The step is clamped so that an asymmetric
     *  distribution (max >> avg) doesn't compress most of the range into one bucket. */
    void computeValueRangeForMap(const T::ParamValue_vec& values,
                                 double& minValue,
                                 double& maxValue,
                                 double& step);

    int page_map(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_main(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_dispatch(const std::string& page,
                      const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session,
                      int& expires_seconds);


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
                      const std::string& landMask,
                      const std::string& seaMask,
                      const std::string& colorMapName,
                      const std::string& missingStr,
                      const MapArea* area = nullptr);


    T::ColorMapFile*  getColorMapFile(const std::string& colorMapName);

    /*! \brief The values of an image: the field in its own geometry, or interpolated to
     *  the projection geometry. With params.fileId2 the values are fileId2 - fileId.
     *  Returns false if the values or the geometry are not available. */
    bool getImageValues(const ImagePaintParameters& params,T::GeometryId& geometryId,uint& columns,uint& rows,T::ParamValue_vec& values);

    // ### The new user interface at /gui (Gui.cpp) and its JSON API (Api.cpp)

    int gui_request(const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      const std::string& path);

    int gui_file(const std::string& name,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    int gui_render(const std::string& page,
                      const Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      int& expires_seconds);

    int gui_api(const std::string& name,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      int& expires_seconds);

    Json::Value api_meta();
    Json::Value api_producers();
    Json::Value api_generations(const Spine::HTTP::Request& theRequest);
    Json::Value api_parameters(const Spine::HTTP::Request& theRequest);
    Json::Value api_contents(const Spine::HTTP::Request& theRequest);
    Json::Value api_key(const Spine::HTTP::Request& theRequest);
    Json::Value api_value(const Spine::HTTP::Request& theRequest);
    Json::Value api_series(const Spine::HTTP::Request& theRequest);
    Json::Value api_profile(const Spine::HTTP::Request& theRequest);
    Json::Value api_stats(const Spine::HTTP::Request& theRequest);
    Json::Value api_colormap(const Spine::HTTP::Request& theRequest);
    Json::Value api_info(const Spine::HTTP::Request& theRequest);
    Json::Value api_sample(const Spine::HTTP::Request& theRequest);

    void checkImageCache();
    void getGenerations(T::GenerationInfoList& generationInfoList,std::set<std::string>& generations);
    void getLevelIds(const T::ContentInfoList& contentInfoList,std::set<int>& levelIds);
    void getLevels(const T::ContentInfoList& contentInfoList,int levelId,std::set<int>& levels);
    std::string getFmiKey(const std::string& producerName,const T::ContentInfo& contentInfo);
    void getForecastTypes(const T::ContentInfoList& contentInfoList,int levelId,int level,std::set<int>& forecastTypes);
    void getForecastNumbers(const T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,std::set<int>& forecastNumbers);
    void getGeometries(const T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,int forecastNumber,std::set<int>& geometries);
    uint getColorValue(const std::string& colorName);
    void initSession(Session& session);
    void loadColorFile();
    void loadProducerFile();
    bool loadImage(const char *fname,Spine::HTTP::Response &theResponse);
    bool loadCachedImage(const std::string& hash,Spine::HTTP::Response &theResponse);
    bool isImageUnderConstruction(const std::string& hash);
    uint startImageConstruction(const std::string& hash);
    void endImageConstruction(uint idx);
    void addCachedImage(const std::string& hash,const std::string& fname);
    bool waitForCachedImage(const std::string& hash,Spine::HTTP::Response &theResponse);
    std::string colorMapVersion(const std::string& colorMap);

  

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

    std::string               itsUiUrl;                         //!< URL of the new user interface (default /gui).
    std::string               itsUiDirectory;                   //!< Directory of the files of the new user interface.
    std::map<std::string,UiFile> itsUiFiles;                    //!< Files of the new user interface by name.
    std::mutex                itsUiFilesMutex;                  //!< Lock of itsUiFiles.

    std::shared_ptr<Engine::Grid::Engine> itsGridEngine;        //!< Grid engine used for content and data server access.
    std::map<std::string,std::string>      itsImages;           //!< Maps image hash keys to cached image file paths.
};  // class Plugin

}  // namespace GridGui
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
