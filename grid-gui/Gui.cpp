// ======================================================================
/*!
 * \brief The new user interface of grid-gui
 *
 * Everything below the user interface URL (default /gui):
 *
 *   /gui/                 the page (ui/index.html)
 *   /gui/ui/NAME          a file of the page (JavaScript modules, CSS) from ui.directory
 *   /gui/api/NAME         the JSON API of the page (Api.cpp)
 *   /gui/image?...        rendered images with readable parameters, drawn by the same
 *   /gui/map?...          code and kept in the same image cache as the legacy pages
 *   ...
 *
 * The legacy interface at /grid-gui is not affected.
 */
// ======================================================================

#include "Attributes.h"
#include "Plugin.h"

#include <grid-files/common/GeneralFunctions.h>
#include <macgyver/Hash.h>
#include <macgyver/TimeFormatter.h>
#include <spine/HTTP.h>
#include <cctype>
#include <fstream>
#include <sstream>

namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{

using namespace SmartMet::Spine;

namespace
{
// The readable request parameters of the rendering pages and the session attributes
// they set. The defaults come from Plugin::initSession().

const struct
{
  const char* parameter;
  const char* attribute;
} renderParameters[] = {{"file", ATTR_FILE_ID},
                        {"message", ATTR_MESSAGE_INDEX},
                        {"geometry", ATTR_GEOMETRY_ID},
                        {"projection", ATTR_PROJECTION_ID},
                        {"colormap", ATTR_COLOR_MAP},
                        {"opacity", ATTR_OPACITY},
                        {"hue", ATTR_HUE},
                        {"saturation", ATTR_SATURATION},
                        {"blur", ATTR_BLUR},
                        {"coordinateLines", ATTR_COORDINATE_LINES},
                        {"landBorder", ATTR_LAND_BORDER},
                        {"landColor", ATTR_LAND_MASK},
                        {"landColorPosition", ATTR_LAND_COLOR_POS},
                        {"seaColor", ATTR_SEA_MASK},
                        {"seaColorPosition", ATTR_SEA_COLOR_POS},
                        {"landShadingLight", ATTR_LAND_SHADING_LIGHT},
                        {"landShadingShadow", ATTR_LAND_SHADING_SHADOW},
                        {"landShadingPosition", ATTR_LAND_SHADING_POS},
                        {"seaShadingLight", ATTR_SEA_SHADING_LIGHT},
                        {"seaShadingShadow", ATTR_SEA_SHADING_SHADOW},
                        {"seaShadingPosition", ATTR_SEA_SHADING_POS},
                        {"missing", ATTR_MISSING},
                        {"step", ATTR_STEP},
                        {"minLength", ATTR_MIN_LENGTH},
                        {"maxLength", ATTR_MAX_LENGTH},
                        {"streamColor", ATTR_STREAM_COLOR},
                        {"file2", ATTR_FILE_ID2},
                        {"message2", ATTR_MESSAGE_INDEX2},
                        {"range", ATTR_DIFFERENCE_RANGE},
                        {"area", ATTR_MAP_AREA},
                        {"scale", ATTR_VALUE_SCALE},
                        {"offset", ATTR_VALUE_OFFSET}};

// Only plain file names are served: letters, digits, '-', '_' and one '.' before a known
// extension. This keeps requests inside ui.directory.

const char* uiContentType(const std::string& name)
{
  if (name.empty() || name.size() > 64)
    return nullptr;

  std::size_t dots = 0;
  for (char ch : name)
  {
    if (ch == '.')
      dots++;
    else if (!isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_')
      return nullptr;
  }
  if (dots != 1 || name[0] == '.')
    return nullptr;

  const std::string ext = name.substr(name.find('.') + 1);
  if (ext == "js")
    return "text/javascript; charset=UTF-8";
  if (ext == "css")
    return "text/css; charset=UTF-8";
  if (ext == "html")
    return "text/html; charset=UTF-8";
  if (ext == "svg")
    return "image/svg+xml";
  if (ext == "json")
    return "application/json; charset=UTF-8";
  return nullptr;
}

void setCacheHeaders(HTTP::Response& theResponse, int expires_seconds)
{
  Fmi::DateTime t_now = Fmi::SecondClock::universal_time();
  Fmi::DateTime t_expires = t_now + Fmi::Seconds(expires_seconds);
  std::shared_ptr<Fmi::TimeFormatter> tformat(Fmi::TimeFormatter::create("http"));
  theResponse.setHeader("Cache-Control", "public, max-age=" + std::to_string(expires_seconds));
  theResponse.setHeader("Expires", tformat->format(t_expires));
}

int notFound(HTTP::Response& theResponse)
{
  theResponse.setHeader("Content-Type", "text/plain; charset=UTF-8");
  theResponse.setContent("Not found\n");
  return HTTP::Status::not_found;
}

}  // namespace

/*! \brief Serves a request below the user interface URL. The path is the part of the
    resource after the URL, for example "/api/producers". */

int Plugin::gui_request(const Spine::Reactor& theReactor,
                        const HTTP::Request& theRequest,
                        HTTP::Response& theResponse,
                        const std::string& path)
{
  try
  {
    // Relative references in the page need the trailing slash
    if (path.empty())
    {
      std::string location = itsUiUrl + "/";
      const std::string query = theRequest.getQueryString();
      if (!query.empty())
        location += (query[0] == '?' ? query : "?" + query);
      theResponse.setHeader("Location", location);
      theResponse.setContent("");
      return HTTP::Status::moved_permanently;
    }

    if (path == "/")
      return gui_file("index.html", theRequest, theResponse);

    if (path.compare(0, 4, "/ui/") == 0)
      return gui_file(path.substr(4), theRequest, theResponse);

    int expires_seconds = 1;
    int status = HTTP::Status::not_found;

    if (path.compare(0, 5, "/api/") == 0)
      status = gui_api(path.substr(5), theRequest, theResponse, expires_seconds);
    else
      status = gui_render(path.substr(1), theReactor, theRequest, theResponse, expires_seconds);

    if (status == HTTP::Status::ok)
      setCacheHeaders(theResponse, expires_seconds);
    return status;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!").addParameter("Path", path);
  }
}

/*! \brief Serves a file of the page from ui.directory. The files are kept in memory and
    read again when they change, so that the page can be edited without a restart. */

int Plugin::gui_file(const std::string& name,
                     const HTTP::Request& theRequest,
                     HTTP::Response& theResponse)
{
  try
  {
    const char* contentType = uiContentType(name);
    if (contentType == nullptr)
      return notFound(theResponse);

    const std::string filename = itsUiDirectory + "/" + name;
    const time_t modificationTime = getFileModificationTime(filename.c_str());
    if (modificationTime == 0)
      return notFound(theResponse);

    std::string content;
    std::string etag;
    {
      std::lock_guard<std::mutex> lock(itsUiFilesMutex);
      auto& file = itsUiFiles[name];
      if (file.modificationTime != modificationTime)
      {
        std::ifstream in(filename, std::ios::binary);
        if (!in)
          return notFound(theResponse);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        file.content = buffer.str();
        file.modificationTime = modificationTime;
        file.etag = "\"gui-" + std::to_string(Fmi::hash_value(file.content)) + "\"";
      }
      content = file.content;
      etag = file.etag;
    }

    theResponse.setHeader("ETag", etag);
    // Revalidated on every use: an edited file is taken into use at once
    theResponse.setHeader("Cache-Control", "no-cache");
    theResponse.setHeader("Content-Type", contentType);

    if (auto status = conditionalResponseStatus(theRequest, etag))
      return *status;

    theResponse.setContent(content);
    return HTTP::Status::ok;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!").addParameter("File", name);
  }
}

/*! \brief Renders an image, a sample table, a message dump or a download with the
    readable parameters of the new user interface. The page functions of the legacy
    interface do the work, so the images share the image cache. */

int Plugin::gui_render(const std::string& page,
                       const Spine::Reactor& theReactor,
                       const HTTP::Request& theRequest,
                       HTTP::Response& theResponse,
                       int& expires_seconds)
{
  try
  {
    using PageHandler = int (Plugin::*)(
        const Spine::Reactor&, const HTTP::Request&, HTTP::Response&, Session&);

    struct RenderPage
    {
      const char* name;
      PageHandler handler;
      int expires_seconds;
    };

    static const RenderPage pages[] = {{"image", &Plugin::page_image, 600},
                                       {"difference", &Plugin::page_image, 600},
                                       {"map", &Plugin::page_map, 600},
                                       {"streams", &Plugin::page_streams, 600},
                                       {"streamsAnimation", &Plugin::page_streamsAnimation, 600},
                                       {"table", &Plugin::page_table, 600},
                                       {"coordinates", &Plugin::page_coordinates, 600},
                                       {"message", &Plugin::page_message, 600},
                                       {"download", &Plugin::page_download, 600}};

    for (const auto& info : pages)
    {
      if (page != info.name)
        continue;

      if (!theRequest.getParameter("file"))
      {
        theResponse.setHeader("Content-Type", "text/plain; charset=UTF-8");
        theResponse.setContent("Parameter 'file' is missing\n");
        return HTTP::Status::bad_request;
      }

      // A difference image needs the second field, an image must not have one
      const bool difference = (page == "difference");
      if (difference != static_cast<bool>(theRequest.getParameter("file2")))
      {
        theResponse.setHeader("Content-Type", "text/plain; charset=UTF-8");
        theResponse.setContent(difference ? "Parameter 'file2' is missing\n"
                                          : "Parameter 'file2' is allowed only in a difference\n");
        return HTTP::Status::bad_request;
      }

      Session session;
      initSession(session);
      for (const auto& p : renderParameters)
      {
        if (auto value = theRequest.getParameter(p.parameter))
          session.setAttribute(p.attribute, *value);
      }

      expires_seconds = info.expires_seconds;
      return (this->*info.handler)(theReactor, theRequest, theResponse, session);
    }

    return notFound(theResponse);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!").addParameter("Page", page);
  }
}

}  // namespace GridGui
}  // namespace Plugin
}  // namespace SmartMet
