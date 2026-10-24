// ======================================================================
/*!
 * \brief The JSON API of the new grid-gui user interface (/gui/api/NAME)
 *
 * The page asks for the producers, generations, parameters and the contents of one
 * parameter, and builds every selection from them without reloading. The other calls
 * explain a parameter key, read values at a point (also as a time series or a vertical
 * profile), give the value range of an image for its legend and describe a message.
 */
// ======================================================================

#include "Attributes.h"
#include "Plugin.h"

#include <grid-files/common/GeneralFunctions.h>
#include <grid-files/identification/GridDef.h>
#include <macgyver/StringConversion.h>
#include <fmt/format.h>
#include <spine/HTTP.h>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{

using namespace SmartMet::Spine;

namespace
{
// An error in the request: answered with 400 and the message

class BadRequest : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

std::string requiredParameter(const HTTP::Request& theRequest, const char* name)
{
  auto value = theRequest.getParameter(name);
  if (!value || value->empty())
    throw BadRequest(std::string("Parameter '") + name + "' is missing");
  return *value;
}

long long requiredInteger(const HTTP::Request& theRequest, const char* name)
{
  const std::string value = requiredParameter(theRequest, name);
  try
  {
    std::size_t pos = 0;
    long long result = std::stoll(value, &pos);
    if (pos != value.size())
      throw BadRequest("");
    return result;
  }
  catch (...)
  {
    throw BadRequest(std::string("Parameter '") + name + "' must be an integer");
  }
}

long long optionalInteger(const HTTP::Request& theRequest, const char* name, long long defaultValue)
{
  auto value = theRequest.getParameter(name);
  if (!value || value->empty())
    return defaultValue;
  return requiredInteger(theRequest, name);
}

double requiredNumber(const HTTP::Request& theRequest, const char* name)
{
  const std::string value = requiredParameter(theRequest, name);
  try
  {
    std::size_t pos = 0;
    double result = std::stod(value, &pos);
    if (pos != value.size() || !std::isfinite(result))
      throw BadRequest("");
    return result;
  }
  catch (...)
  {
    throw BadRequest(std::string("Parameter '") + name + "' must be a number");
  }
}

// A value, or null when it is missing

Json::Value jsonValue(T::ParamValue value)
{
  if (value == ParamValueMissing || !std::isfinite(value))
    return Json::Value(Json::nullValue);
  return Json::Value(static_cast<double>(value));
}

// Names of the interpolation methods (grid-files Typedefs.h)

const char* areaInterpolationName(short method)
{
  switch (method)
  {
    case T::AreaInterpolationMethod::None:
      return "none";
    case T::AreaInterpolationMethod::Linear:
      return "linear";
    case T::AreaInterpolationMethod::Nearest:
      return "nearest";
    case T::AreaInterpolationMethod::Min:
      return "min";
    case T::AreaInterpolationMethod::Max:
      return "max";
    case T::AreaInterpolationMethod::Landscape:
      return "landscape";
    case T::AreaInterpolationMethod::Forbidden:
      return "forbidden";
    case T::AreaInterpolationMethod::List:
      return "list";
    case T::AreaInterpolationMethod::ListWithAngles:
      return "list with angles";
    case T::AreaInterpolationMethod::Undefined:
      return "undefined";
    default:
      return (method >= T::AreaInterpolationMethod::External ? "external" : "unknown");
  }
}

const char* timeInterpolationName(short method)
{
  switch (method)
  {
    case T::TimeInterpolationMethod::None:
      return "none";
    case T::TimeInterpolationMethod::Linear:
      return "linear";
    case T::TimeInterpolationMethod::Nearest:
      return "nearest";
    case T::TimeInterpolationMethod::Min:
      return "min";
    case T::TimeInterpolationMethod::Max:
      return "max";
    case T::TimeInterpolationMethod::Previous:
      return "previous";
    case T::TimeInterpolationMethod::Next:
      return "next";
    case T::TimeInterpolationMethod::Forbidden:
      return "forbidden";
    case T::TimeInterpolationMethod::Transfer:
      return "transfer";
    case T::TimeInterpolationMethod::Undefined:
      return "undefined";
    default:
      return (method >= T::TimeInterpolationMethod::External ? "external" : "unknown");
  }
}

const char* levelInterpolationName(short method)
{
  switch (method)
  {
    case T::LevelInterpolationMethod::None:
      return "none";
    case T::LevelInterpolationMethod::Linear:
      return "linear";
    case T::LevelInterpolationMethod::Nearest:
      return "nearest";
    case T::LevelInterpolationMethod::Min:
      return "min";
    case T::LevelInterpolationMethod::Max:
      return "max";
    case T::LevelInterpolationMethod::Logarithmic:
      return "logarithmic";
    case T::LevelInterpolationMethod::Previous:
      return "previous";
    case T::LevelInterpolationMethod::Next:
      return "next";
    case T::LevelInterpolationMethod::Forbidden:
      return "forbidden";
    case T::LevelInterpolationMethod::Undefined:
      return "undefined";
    default:
      return (method >= T::LevelInterpolationMethod::External ? "external" : "unknown");
  }
}

const char* levelTypeName(uchar type)
{
  switch (type)
  {
    case T::ParamLevelType::Linear:
      return "linear";
    case T::ParamLevelType::Logarithmic:
      return "logarithmic";
    case T::ParamLevelType::Number:
      return "number";
    default:
      return "undefined";
  }
}

Json::Value interpolationJson(short area, short time, short level)
{
  Json::Value json(Json::objectValue);
  json["area"] = area;
  json["areaName"] = areaInterpolationName(area);
  json["time"] = time;
  json["timeName"] = timeInterpolationName(time);
  json["level"] = level;
  json["levelName"] = levelInterpolationName(level);
  return json;
}

Json::Value levelTypeJson(int levelId)
{
  Json::Value json(Json::objectValue);
  json["id"] = levelId;
  Identification::LevelDef def;
  if (levelId >= 0 && Identification::gridDef.getFmiLevelDef(levelId, def))
  {
    json["name"] = def.mName;
    json["description"] = def.mDescription;
    json["units"] = def.mUnits;
    json["type"] = levelTypeName(def.mType);
  }
  return json;
}

Json::Value forecastTypeJson(int forecastType)
{
  Json::Value json(Json::objectValue);
  json["id"] = forecastType;
  Identification::ForecastTypeDef def;
  if (Identification::gridDef.getFmiForecastTypeDef(forecastType, def))
  {
    json["name"] = def.mName;
    json["description"] = def.mDescription;
  }
  return json;
}

Json::Value geometryJson(T::GeometryId geometryId)
{
  Json::Value json(Json::objectValue);
  json["id"] = geometryId;
  std::string name;
  if (Identification::gridDef.getGeometryNameById(geometryId, name))
    json["name"] = name;
  uint cols = 0;
  uint rows = 0;
  if (Identification::gridDef.getGridDimensionsByGeometryId(geometryId, cols, rows))
  {
    json["columns"] = cols;
    json["rows"] = rows;
  }
  return json;
}

// The geometry with its corners and cell size, for the key explanation

Json::Value geometryDetailsJson(T::GeometryId geometryId)
{
  Json::Value json = geometryJson(geometryId);
  T::Coordinate topLeft;
  T::Coordinate topRight;
  T::Coordinate bottomLeft;
  T::Coordinate bottomRight;
  if (Identification::gridDef.getGridLatLonAreaByGeometryId(
          geometryId, topLeft, topRight, bottomLeft, bottomRight))
  {
    auto corner = [](const T::Coordinate& c)
    {
      Json::Value p(Json::arrayValue);
      p.append(c.x());
      p.append(c.y());
      return p;
    };
    Json::Value corners(Json::objectValue);
    corners["topLeft"] = corner(topLeft);
    corners["topRight"] = corner(topRight);
    corners["bottomLeft"] = corner(bottomLeft);
    corners["bottomRight"] = corner(bottomRight);
    json["corners"] = corners;
  }
  double width = 0;
  double height = 0;
  if (Identification::gridDef.getGridCellAverageSizeByGeometryId(geometryId, width, height))
  {
    json["cellWidth"] = width;
    json["cellHeight"] = height;
  }
  return json;
}

// The FMI parameter definition of a parameter name (FMI name or FMI-<id>)

bool fmiParameterDef(const std::string& name, Identification::FmiParameterDef& def)
{
  if (strncasecmp(name.c_str(), "FMI-", 4) == 0)
    return Identification::gridDef.getFmiParameterDefById(toUInt32(name.substr(4)), def);
  return Identification::gridDef.getFmiParameterDefByName(name, def);
}

// The parameter name of a content record, as in its FMI key

std::string contentParameterName(const T::ContentInfo& info)
{
  if (strlen(info.getFmiParameterName()) > 0)
    return info.getFmiParameterName();
  if (info.mFmiParameterId > 0)
    return "FMI-" + std::to_string(info.mFmiParameterId);
  return "";
}

// One time and its content record in a series

struct TimeContent
{
  std::string time;
  T::FileId fileId;
  T::MessageIndex messageIndex;
};

}  // namespace

/*! \brief Serves /gui/api/NAME. The answers depend on the content, which changes all the
    time, so they may be cached only for a few seconds. */

int Plugin::gui_api(const std::string& name,
                    const HTTP::Request& theRequest,
                    HTTP::Response& theResponse,
                    int& expires_seconds)
{
  try
  {
    using ApiHandler = Json::Value (Plugin::*)(const HTTP::Request&);
    static const std::map<std::string, ApiHandler> handlers = {
        {"generations", &Plugin::api_generations},
        {"parameters", &Plugin::api_parameters},
        {"contents", &Plugin::api_contents},
        {"key", &Plugin::api_key},
        {"value", &Plugin::api_value},
        {"series", &Plugin::api_series},
        {"profile", &Plugin::api_profile},
        {"stats", &Plugin::api_stats},
        {"colormap", &Plugin::api_colormap},
        {"info", &Plugin::api_info}};

    Json::Value result;
    int status = HTTP::Status::ok;

    try
    {
      if (name == "meta")
        result = api_meta();
      else if (name == "producers")
        result = api_producers();
      else
      {
        auto it = handlers.find(name);
        if (it == handlers.end())
        {
          status = HTTP::Status::not_found;
          result["error"] = "Unknown API call: " + name;
        }
        else
          result = (this->*(it->second))(theRequest);
      }
    }
    catch (const BadRequest& e)
    {
      status = HTTP::Status::bad_request;
      result = Json::Value(Json::objectValue);
      result["error"] = e.what();
    }

    // Meta data changes only when the configuration does, the rest with the content
    expires_seconds = (name == "meta" || name == "colormap" ? 60 : 5);

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    builder["precision"] = 10;
    theResponse.setHeader("Content-Type", "application/json; charset=UTF-8");
    theResponse.setContent(Json::writeString(builder, result) + "\n");
    return status;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!").addParameter("API", name);
  }
}

/*! \brief The settings of the page: colours, colour maps, layer positions, projections
    and the default rendering options. */

Json::Value Plugin::api_meta()
{
  try
  {
    Json::Value result(Json::objectValue);

    // The colours are not reloaded here: loadColorFile() rewrites itsColors while the
    // rendering threads may read it. The legacy main page reloads a changed file.
    Json::Value colors(Json::arrayValue);
    for (const auto& color : itsColors)
    {
      Json::Value c(Json::objectValue);
      c["name"] = color.first;
      c["argb"] = fmt::format("{:08X}", color.second);
      colors.append(c);
    }
    result["colors"] = colors;

    std::set<std::string> names;
    for (auto& colorMapFile : itsColorMapFiles)
    {
      colorMapFile.checkUpdates();
      for (const auto& n : colorMapFile.getNames())
        names.insert(n);
    }
    Json::Value colorMaps(Json::arrayValue);
    for (const auto& n : names)
      colorMaps.append(n);
    result["colorMaps"] = colorMaps;

    // Every geometry can be a projection except the blocked ones
    std::set<T::GeometryId> geometries;
    Identification::gridDef.getGeometryIdList(geometries);
    Json::Value projections(Json::arrayValue);
    for (auto id : geometries)
    {
      Json::Value g = geometryJson(id);
      if (itsBlockedProjections.find(id) != itsBlockedProjections.end())
        g["blocked"] = true;
      projections.append(g);
    }
    result["projections"] = projections;

    result["animationEnabled"] = itsAnimationEnabled;
    result["legacyUrl"] = "/grid-gui";

    // The defaults of the rendering options are those of the legacy interface
    Session session;
    initSession(session);
    Json::Value defaults(Json::objectValue);
    const struct
    {
      const char* name;
      const char* attribute;
    } options[] = {{"colormap", ATTR_COLOR_MAP},
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
                   {"streamColor", ATTR_STREAM_COLOR}};
    for (const auto& option : options)
      defaults[option.name] = session.getAttribute(option.attribute);
    result["defaults"] = defaults;

    return result;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The producers with the state of their newest generations. */

Json::Value Plugin::api_producers()
{
  try
  {
    auto contentServer = itsGridEngine->getContentServer_sptr();

    T::ProducerInfoList producerInfoList;
    contentServer->getProducerInfoList(0, producerInfoList);
    producerInfoList.sortByName();

    T::GenerationInfoList generationInfoList;
    contentServer->getGenerationInfoList(0, generationInfoList);

    // The number of generations and the newest one, and the newest ready one, by producer
    struct State
    {
      uint count = 0;
      const T::GenerationInfo* newest = nullptr;
      const T::GenerationInfo* newestReady = nullptr;
    };
    std::map<T::ProducerId, State> states;

    const uint glen = generationInfoList.getLength();
    for (uint t = 0; t < glen; t++)
    {
      const T::GenerationInfo* g = generationInfoList.getGenerationInfoByIndex(t);
      auto& state = states[g->mProducerId];
      state.count++;
      if (state.newest == nullptr || state.newest->mAnalysisTime < g->mAnalysisTime)
        state.newest = g;
      if (g->mStatus == T::GenerationInfo::Status::Ready &&
          (state.newestReady == nullptr || state.newestReady->mAnalysisTime < g->mAnalysisTime))
        state.newestReady = g;
    }

    Json::Value result(Json::arrayValue);
    const uint len = producerInfoList.getLength();
    for (uint t = 0; t < len; t++)
    {
      const T::ProducerInfo* p = producerInfoList.getProducerInfoByIndex(t);
      Json::Value producer(Json::objectValue);
      producer["id"] = p->mProducerId;
      producer["name"] = p->mName;
      producer["title"] = p->mTitle;
      producer["description"] = p->mDescription;

      const auto& state = states[p->mProducerId];
      producer["generations"] = state.count;
      if (state.newest != nullptr)
        producer["newest"] = state.newest->mAnalysisTime;
      if (state.newestReady != nullptr)
        producer["newestReady"] = state.newestReady->mAnalysisTime;
      result.append(producer);
    }
    return result;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The generations of a producer, newest first. Generations about to be deleted
    are left out, as in the legacy interface. */

Json::Value Plugin::api_generations(const HTTP::Request& theRequest)
{
  try
  {
    const auto producerId = static_cast<T::ProducerId>(requiredInteger(theRequest, "producer"));
    auto contentServer = itsGridEngine->getContentServer_sptr();

    T::GenerationInfoList all;
    contentServer->getGenerationInfoListByProducerId(0, producerId, all);
    T::GenerationInfoList generationInfoList;
    all.getGenerationInfoListByProducerId(producerId, generationInfoList);

    const time_t requiredAccessTime = time(nullptr) + 120;

    std::vector<const T::GenerationInfo*> generations;
    const uint len = generationInfoList.getLength();
    for (uint t = 0; t < len; t++)
    {
      const T::GenerationInfo* g = generationInfoList.getGenerationInfoByIndex(t);
      if (g->mDeletionTime == 0 || g->mDeletionTime > requiredAccessTime)
        generations.push_back(g);
    }
    std::sort(generations.begin(),
              generations.end(),
              [](const T::GenerationInfo* a, const T::GenerationInfo* b)
              { return std::tie(a->mAnalysisTime, a->mName) > std::tie(b->mAnalysisTime, b->mName); });

    Json::Value result(Json::arrayValue);
    for (const auto* g : generations)
    {
      Json::Value generation(Json::objectValue);
      generation["id"] = Json::UInt64(g->mGenerationId);
      generation["name"] = g->mName;
      generation["analysisTime"] = g->mAnalysisTime;
      generation["ready"] = (g->mStatus == T::GenerationInfo::Status::Ready);
      if (g->mContentStartTime > 0)
        generation["start"] = utcTimeFromTimeT(g->mContentStartTime);
      if (g->mContentEndTime > 0)
        generation["end"] = utcTimeFromTimeT(g->mContentEndTime);
      if (g->mDeletionTime > 0)
        generation["deletion"] = utcTimeFromTimeT(g->mDeletionTime);
      result.append(generation);
    }
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The parameters of a generation with their descriptions, units and the other
    names the parameter mappings of the producer give to them. */

Json::Value Plugin::api_parameters(const HTTP::Request& theRequest)
{
  try
  {
    const auto generationId = static_cast<T::GenerationId>(requiredInteger(theRequest, "generation"));
    auto contentServer = itsGridEngine->getContentServer_sptr();

    T::GenerationInfo generationInfo;
    if (contentServer->getGenerationInfoById(0, generationId, generationInfo) != 0)
      throw BadRequest("Unknown generation");

    T::ProducerInfo producerInfo;
    contentServer->getProducerInfoById(0, generationInfo.mProducerId, producerInfo);

    std::set<std::string> keys;
    contentServer->getContentParamKeyListByGenerationId(
        0, generationId, T::ParamKeyTypeValue::FMI_NAME, keys);

    // The names are FMI names, or FMI-<id> for parameters without one
    std::set<std::string> fmiNames;
    std::vector<std::pair<std::string, Identification::FmiParameterDef>> parameters;
    for (const auto& key : keys)
    {
      Identification::FmiParameterDef def;
      if (fmiParameterDef(key, def))
      {
        parameters.emplace_back(def.mParameterName, def);
        fmiNames.insert(def.mParameterName);
      }
      else
        parameters.emplace_back(key, def);
    }

    std::map<std::string, std::set<std::string>> aliases;
    itsGridEngine->getParameterMappingNames(producerInfo.mName, fmiNames, aliases);

    Json::Value result(Json::arrayValue);
    for (const auto& p : parameters)
    {
      Json::Value parameter(Json::objectValue);
      parameter["name"] = p.first;
      parameter["description"] = p.second.mParameterDescription;
      parameter["units"] = p.second.mParameterUnits;
      if (p.second.mFmiParameterId > 0)
        parameter["fmiId"] = p.second.mFmiParameterId;
      auto it = aliases.find(p.first);
      if (it != aliases.end())
      {
        Json::Value names(Json::arrayValue);
        for (const auto& n : it->second)
          names.append(n);
        parameter["aliases"] = names;
      }
      result.append(parameter);
    }
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The fields of a parameter in a generation. The fields with the same level type,
    level, forecast type, forecast number and geometry are one entry with the times and
    the file and message of each time. The level types, forecast types and geometries
    used are described once. */

Json::Value Plugin::api_contents(const HTTP::Request& theRequest)
{
  try
  {
    const auto generationId = static_cast<T::GenerationId>(requiredInteger(theRequest, "generation"));
    const std::string parameter = requiredParameter(theRequest, "parameter");
    auto contentServer = itsGridEngine->getContentServer_sptr();

    T::GenerationInfo generationInfo;
    if (contentServer->getGenerationInfoById(0, generationId, generationInfo) != 0)
      throw BadRequest("Unknown generation");

    T::ProducerInfo producerInfo;
    contentServer->getProducerInfoById(0, generationInfo.mProducerId, producerInfo);

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByParameterAndGenerationId(0,
                                                            generationId,
                                                            T::ParamKeyTypeValue::FMI_NAME,
                                                            parameter,
                                                            -1,
                                                            0,
                                                            0,
                                                            -1,
                                                            -1,
                                                            -1,
                                                            "14000101T000000",
                                                            "30000101T000000",
                                                            0,
                                                            contentInfoList);
    contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiName_producer_generation_level_time);

    using FieldKey = std::tuple<int, int, int, int, int>;  // level id, level, type, number, geometry
    std::map<FieldKey, std::vector<TimeContent>> fields;
    std::set<int> levelIds;
    std::set<int> forecastTypes;
    std::set<T::GeometryId> geometries;

    const uint len = contentInfoList.getLength();
    for (uint t = 0; t < len; t++)
    {
      const T::ContentInfo* c = contentInfoList.getContentInfoByIndex(t);
      FieldKey key{c->mFmiParameterLevelId,
                   c->mParameterLevel,
                   c->mForecastType,
                   c->mForecastNumber,
                   c->mGeometryId};
      fields[key].push_back(TimeContent{c->getForecastTime(), c->mFileId, c->mMessageIndex});
      levelIds.insert(c->mFmiParameterLevelId);
      forecastTypes.insert(c->mForecastType);
      geometries.insert(c->mGeometryId);
    }

    loadProducerFile();

    Json::Value result(Json::objectValue);
    result["producer"] = producerInfo.mName;
    result["generation"] = Json::UInt64(generationId);
    result["parameter"] = parameter;

    Json::Value levelTypes(Json::objectValue);
    for (int id : levelIds)
      levelTypes[std::to_string(id)] = levelTypeJson(id);
    result["levelTypes"] = levelTypes;

    Json::Value types(Json::objectValue);
    for (int id : forecastTypes)
      types[std::to_string(id)] = forecastTypeJson(id);
    result["forecastTypes"] = types;

    Json::Value geoms(Json::objectValue);
    for (auto id : geometries)
    {
      Json::Value g = geometryJson(id);
      // Queries find only the producer geometries listed in the producer file
      const std::string producerGeometry = toUpperString(producerInfo.mName + ":" + std::to_string(id));
      g["searchConfigured"] = (itsProducerList.find(producerGeometry) != itsProducerList.end());
      geoms[std::to_string(id)] = g;
    }
    result["geometries"] = geoms;

    Json::Value list(Json::arrayValue);
    for (const auto& field : fields)
    {
      Json::Value f(Json::objectValue);
      f["levelId"] = std::get<0>(field.first);
      f["level"] = std::get<1>(field.first);
      f["forecastType"] = std::get<2>(field.first);
      f["forecastNumber"] = std::get<3>(field.first);
      f["geometry"] = std::get<4>(field.first);
      Json::Value times(Json::arrayValue);
      Json::Value files(Json::arrayValue);
      Json::Value messages(Json::arrayValue);
      for (const auto& tc : field.second)
      {
        times.append(tc.time);
        files.append(Json::UInt64(tc.fileId));
        messages.append(tc.messageIndex);
      }
      f["times"] = times;
      f["files"] = files;
      f["messages"] = messages;
      list.append(f);
    }
    result["fields"] = list;
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The parameter key of a field and the meaning of each of its parts, and the
    interpolation methods a query of the key uses. */

Json::Value Plugin::api_key(const HTTP::Request& theRequest)
{
  try
  {
    const auto fileId = static_cast<T::FileId>(requiredInteger(theRequest, "file"));
    const auto messageIndex = static_cast<T::MessageIndex>(requiredInteger(theRequest, "message"));
    auto contentServer = itsGridEngine->getContentServer_sptr();

    T::ContentInfo info;
    if (contentServer->getContentInfo(0, fileId, messageIndex, info) != 0)
      throw BadRequest("Unknown file or message");

    T::ProducerInfo producerInfo;
    contentServer->getProducerInfoById(0, info.mProducerId, producerInfo);

    T::GenerationInfo generationInfo;
    contentServer->getGenerationInfoById(0, info.mGenerationId, generationInfo);

    const std::string parameterName = contentParameterName(info);

    Json::Value result(Json::objectValue);
    result["key"] = getFmiKey(producerInfo.mName, info);

    // The parts of the key, in the order of the key

    Json::Value parameter(Json::objectValue);
    parameter["name"] = parameterName;
    Identification::FmiParameterDef def;
    const bool haveDef = fmiParameterDef(parameterName, def);
    if (haveDef)
    {
      parameter["description"] = def.mParameterDescription;
      parameter["units"] = def.mParameterUnits;
      parameter["fmiId"] = def.mFmiParameterId;
    }
    result["parameter"] = parameter;

    Json::Value producer(Json::objectValue);
    producer["id"] = producerInfo.mProducerId;
    producer["name"] = producerInfo.mName;
    producer["title"] = producerInfo.mTitle;
    producer["description"] = producerInfo.mDescription;
    result["producer"] = producer;

    result["geometry"] = geometryDetailsJson(info.mGeometryId);
    result["levelType"] = levelTypeJson(info.mFmiParameterLevelId);
    result["level"] = info.mParameterLevel;
    result["forecastType"] = forecastTypeJson(info.mForecastType);
    result["forecastNumber"] = info.mForecastNumber;

    // Not part of the key, but describes the field

    Json::Value generation(Json::objectValue);
    generation["id"] = Json::UInt64(generationInfo.mGenerationId);
    generation["name"] = generationInfo.mName;
    generation["analysisTime"] = generationInfo.mAnalysisTime;
    result["generation"] = generation;
    result["time"] = info.getForecastTime();

    if (info.mAggregationId > 0)
    {
      Json::Value aggregation(Json::objectValue);
      aggregation["id"] = info.mAggregationId;
      Identification::AggregationDef aggregationDef;
      if (Identification::gridDef.getFmiAggregationDef(info.mAggregationId, aggregationDef))
      {
        aggregation["name"] = aggregationDef.mName;
        aggregation["description"] = aggregationDef.mDescription;
      }
      if (info.mAggregationPeriod > 0)
        aggregation["periodMinutes"] = info.mAggregationPeriod;
      result["aggregation"] = aggregation;
    }

    if (info.mProcessingTypeId > 0)
    {
      Json::Value processing(Json::objectValue);
      processing["id"] = info.mProcessingTypeId;
      Identification::ProcessingTypeDef processingDef;
      if (Identification::gridDef.getFmiProcessingTypeDef(info.mProcessingTypeId, processingDef))
      {
        processing["name"] = processingDef.mName;
        processing["description"] = processingDef.mDescription;
      }
      processing["value1"] = jsonValue(info.mProcessingTypeValue1);
      processing["value2"] = jsonValue(info.mProcessingTypeValue2);
      result["processing"] = processing;
    }

    // The interpolation methods of a query of the key: those of the parameter mapping of
    // the FMI name, or the defaults of the parameter definition when there is none

    QueryServer::ParameterMapping_vec mappings;
    itsGridEngine->getParameterMappings(producerInfo.mName,
                                        parameterName,
                                        info.mGeometryId,
                                        info.mFmiParameterLevelId,
                                        info.mParameterLevel,
                                        false,
                                        mappings);
    if (mappings.empty())
      itsGridEngine->getParameterMappings(
          producerInfo.mName, parameterName, info.mGeometryId, false, mappings);

    if (!mappings.empty())
    {
      const auto& m = mappings.front();
      Json::Value interpolation = interpolationJson(
          m.mAreaInterpolationMethod, m.mTimeInterpolationMethod, m.mLevelInterpolationMethod);
      interpolation["source"] = "mapping";
      result["interpolation"] = interpolation;
    }
    else if (haveDef)
    {
      Json::Value interpolation = interpolationJson(def.mAreaInterpolationMethod,
                                                    def.mTimeInterpolationMethod,
                                                    def.mLevelInterpolationMethod);
      interpolation["source"] = "parameter";
      result["interpolation"] = interpolation;
    }

    // The other names queries may use for the parameter
    std::map<std::string, std::set<std::string>> aliases;
    itsGridEngine->getParameterMappingNames(producerInfo.mName, {parameterName}, aliases);
    Json::Value names(Json::arrayValue);
    for (const auto& n : aliases[parameterName])
      names.append(n);
    result["aliases"] = names;

    // Is the producer geometry in the producer file, that is, will queries find it?
    loadProducerFile();
    const std::string producerGeometry =
        toUpperString(producerInfo.mName + ":" + std::to_string(info.mGeometryId));
    result["searchConfigured"] = (itsProducerList.find(producerGeometry) != itsProducerList.end());

    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The value at a point of an image with its coordinates. The point is either a
    relative position x,y (0..1 from the top left corner) of the image drawn in the
    projection geometry, or lat,lon. */

Json::Value Plugin::api_value(const HTTP::Request& theRequest)
{
  try
  {
    const auto fileId = static_cast<T::FileId>(requiredInteger(theRequest, "file"));
    const auto messageIndex = static_cast<T::MessageIndex>(requiredInteger(theRequest, "message"));
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    T::ContentInfo info;
    if (contentServer->getContentInfo(0, fileId, messageIndex, info) != 0 || info.mGeometryId == 0)
      throw BadRequest("Unknown file or message");

    double lat = 0;
    double lon = 0;
    const bool haveLatLon = static_cast<bool>(theRequest.getParameter("lat"));

    if (haveLatLon)
    {
      lat = requiredNumber(theRequest, "lat");
      lon = requiredNumber(theRequest, "lon");
    }
    else
    {
      // The cell of the image under the position, and its coordinates. The image rows run
      // from the north as in Plugin::saveImage().
      const double x = requiredNumber(theRequest, "x");
      const double y = requiredNumber(theRequest, "y");
      auto projectionId = static_cast<T::GeometryId>(optionalInteger(theRequest, "projection", 0));
      if (projectionId <= 0)
        projectionId = info.mGeometryId;

      uint cols = 0;
      uint rows = 0;
      if (!Identification::gridDef.getGridDimensionsByGeometryId(projectionId, cols, rows) ||
          cols == 0 || rows == 0)
        throw BadRequest("Unknown projection");

      T::Coordinate_svec coordinates =
          Identification::gridDef.getGridLatLonCoordinatesByGeometryId(projectionId);
      if (!coordinates || coordinates->size() < static_cast<std::size_t>(cols) * rows)
        throw BadRequest("The coordinates of the projection are not available");

      const auto& coords = *coordinates;
      const bool rotate = coords.size() > C_UINT(10 * cols) && coords[0].y() < coords[10 * cols].y();

      auto cell = [](double pos, uint count)
      { return std::max(0, std::min(static_cast<int>(count) - 1, static_cast<int>(pos * count))); };

      const int col = cell(x, cols);
      const int row = cell(y, rows);
      const int dataRow = rotate ? static_cast<int>(rows) - row - 1 : row;
      const auto& p = coords[static_cast<std::size_t>(dataRow) * cols + col];
      lon = p.x();
      lat = p.y();
    }

    // Nearest value in the own geometry (the image shows the grid cells as they are),
    // interpolated when the image is in another geometry
    auto projectionId = static_cast<T::GeometryId>(optionalInteger(theRequest, "projection", 0));
    const bool ownGeometry = (projectionId <= 0 || projectionId == info.mGeometryId);
    const short method = (ownGeometry && !haveLatLon ? T::AreaInterpolationMethod::Nearest
                                                     : T::AreaInterpolationMethod::Linear);

    T::ParamValue value = ParamValueMissing;
    double_vec modificationParameters;
    dataServer->getGridValueByPoint(0,
                                    fileId,
                                    messageIndex,
                                    T::CoordinateTypeValue::LATLON_COORDINATES,
                                    lon,
                                    lat,
                                    method,
                                    0,
                                    modificationParameters,
                                    value);

    Json::Value result(Json::objectValue);
    result["value"] = jsonValue(value);
    result["lat"] = lat;
    result["lon"] = lon;

    double i = 0;
    double j = 0;
    if (Identification::gridDef.getGridPointByGeometryIdAndLatLonCoordinates(info.mGeometryId, lat, lon, i, j))
    {
      result["i"] = i;
      result["j"] = j;
    }
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The values of a field at lat,lon for all its times (generation, parameter,
    levelId, level, forecastType, forecastNumber, geometry). */

Json::Value Plugin::api_series(const HTTP::Request& theRequest)
{
  try
  {
    const auto generationId = static_cast<T::GenerationId>(requiredInteger(theRequest, "generation"));
    const std::string parameter = requiredParameter(theRequest, "parameter");
    const auto levelId = static_cast<T::ParamLevelId>(requiredInteger(theRequest, "levelId"));
    const auto level = static_cast<T::ParamLevel>(requiredInteger(theRequest, "level"));
    const auto forecastType = static_cast<T::ForecastType>(requiredInteger(theRequest, "forecastType"));
    const auto forecastNumber = static_cast<T::ForecastNumber>(requiredInteger(theRequest, "forecastNumber"));
    const auto geometryId = static_cast<T::GeometryId>(requiredInteger(theRequest, "geometry"));
    const double lat = requiredNumber(theRequest, "lat");
    const double lon = requiredNumber(theRequest, "lon");

    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByParameterAndGenerationId(0,
                                                            generationId,
                                                            T::ParamKeyTypeValue::FMI_NAME,
                                                            parameter,
                                                            levelId,
                                                            level,
                                                            level,
                                                            forecastType,
                                                            forecastNumber,
                                                            geometryId,
                                                            "14000101T000000",
                                                            "30000101T000000",
                                                            0,
                                                            contentInfoList);
    contentInfoList.sort(T::ContentInfo::ComparisonMethod::fmiName_producer_generation_level_time);

    Json::Value times(Json::arrayValue);
    Json::Value values(Json::arrayValue);
    const uint len = contentInfoList.getLength();
    for (uint t = 0; t < len; t++)
    {
      const T::ContentInfo* c = contentInfoList.getContentInfoByIndex(t);
      // A negative forecast number in the query matches all members
      if (c->mForecastNumber != forecastNumber)
        continue;

      T::ParamValue value = ParamValueMissing;
      double_vec modificationParameters;
      dataServer->getGridValueByPoint(0,
                                      c->mFileId,
                                      c->mMessageIndex,
                                      T::CoordinateTypeValue::LATLON_COORDINATES,
                                      lon,
                                      lat,
                                      T::AreaInterpolationMethod::Linear,
                                      0,
                                      modificationParameters,
                                      value);
      times.append(c->getForecastTime());
      values.append(jsonValue(value));
    }

    Json::Value result(Json::objectValue);
    result["lat"] = lat;
    result["lon"] = lon;
    result["times"] = times;
    result["values"] = values;
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The values of a field at lat,lon for all levels of its level type at one time
    (generation, parameter, levelId, forecastType, forecastNumber, geometry, time). */

Json::Value Plugin::api_profile(const HTTP::Request& theRequest)
{
  try
  {
    const auto generationId = static_cast<T::GenerationId>(requiredInteger(theRequest, "generation"));
    const std::string parameter = requiredParameter(theRequest, "parameter");
    const auto levelId = static_cast<T::ParamLevelId>(requiredInteger(theRequest, "levelId"));
    const auto forecastType = static_cast<T::ForecastType>(requiredInteger(theRequest, "forecastType"));
    const auto forecastNumber = static_cast<T::ForecastNumber>(requiredInteger(theRequest, "forecastNumber"));
    const auto geometryId = static_cast<T::GeometryId>(requiredInteger(theRequest, "geometry"));
    const std::string time = requiredParameter(theRequest, "time");
    const double lat = requiredNumber(theRequest, "lat");
    const double lon = requiredNumber(theRequest, "lon");

    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    T::ContentInfoList contentInfoList;
    contentServer->getContentListByParameterAndGenerationId(0,
                                                            generationId,
                                                            T::ParamKeyTypeValue::FMI_NAME,
                                                            parameter,
                                                            levelId,
                                                            -1000000000,
                                                            1000000000,
                                                            forecastType,
                                                            forecastNumber,
                                                            geometryId,
                                                            time,
                                                            time,
                                                            0,
                                                            contentInfoList);

    std::map<int, const T::ContentInfo*> levels;
    const uint len = contentInfoList.getLength();
    for (uint t = 0; t < len; t++)
    {
      const T::ContentInfo* c = contentInfoList.getContentInfoByIndex(t);
      if (c->mForecastNumber == forecastNumber && time == c->getForecastTime())
        levels[c->mParameterLevel] = c;
    }

    Json::Value levelList(Json::arrayValue);
    Json::Value values(Json::arrayValue);
    for (const auto& item : levels)
    {
      const T::ContentInfo* c = item.second;
      T::ParamValue value = ParamValueMissing;
      double_vec modificationParameters;
      dataServer->getGridValueByPoint(0,
                                      c->mFileId,
                                      c->mMessageIndex,
                                      T::CoordinateTypeValue::LATLON_COORDINATES,
                                      lon,
                                      lat,
                                      T::AreaInterpolationMethod::Linear,
                                      0,
                                      modificationParameters,
                                      value);
      levelList.append(item.first);
      values.append(jsonValue(value));
    }

    Json::Value result(Json::objectValue);
    result["lat"] = lat;
    result["lon"] = lon;
    result["levelType"] = levelTypeJson(levelId);
    result["levels"] = levelList;
    result["values"] = values;
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The value range of an image, for its legend: the minimum, maximum and mean of
    the values and the scale of the image colours when there is no colour map. With
    file2 the values are the differences file2 - file and the range is that of the
    difference colour scale. */

Json::Value Plugin::api_stats(const HTTP::Request& theRequest)
{
  try
  {
    ImagePaintParameters params;
    params.fileId = static_cast<T::FileId>(requiredInteger(theRequest, "file"));
    params.messageIndex = static_cast<T::MessageIndex>(requiredInteger(theRequest, "message"));
    params.geometryId = static_cast<T::GeometryId>(requiredInteger(theRequest, "geometry"));
    params.projectionId = static_cast<T::GeometryId>(optionalInteger(theRequest, "projection", 0));
    if (theRequest.getParameter("file2"))
    {
      params.fileId2 = static_cast<T::FileId>(requiredInteger(theRequest, "file2"));
      params.messageIndex2 = static_cast<T::MessageIndex>(requiredInteger(theRequest, "message2"));
    }

    T::GeometryId geometryId = 0;
    uint columns = 0;
    uint rows = 0;
    T::ParamValue_vec values;
    if (!getImageValues(params, geometryId, columns, rows, values))
      throw BadRequest("The values are not available");

    const HsvScale scale = hsvScale(values);

    Json::Value result(Json::objectValue);
    result["count"] = scale.count;
    result["missing"] = static_cast<Json::UInt64>(values.size() - scale.count);
    if (scale.count > 0)
    {
      result["min"] = scale.minValue;
      result["max"] = scale.maxValue;
      result["mean"] = scale.avgValue;
      result["step"] = scale.step;
    }
    if (params.fileId2 != 0)
      result["range"] = differenceRange(values);
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The values and ARGB colours of a colour map, for its legend. */

Json::Value Plugin::api_colormap(const HTTP::Request& theRequest)
{
  try
  {
    const std::string name = requiredParameter(theRequest, "name");
    T::ColorMapFile* colorMapFile = getColorMapFile(name);
    if (colorMapFile == nullptr)
      throw BadRequest("Unknown colour map");

    std::vector<float> values;
    std::vector<unsigned int> colors;
    colorMapFile->getValuesAndColors(values, colors);

    Json::Value result(Json::objectValue);
    result["name"] = name;
    Json::Value stops(Json::arrayValue);
    for (std::size_t t = 0; t < values.size() && t < colors.size(); t++)
    {
      Json::Value stop(Json::arrayValue);
      stop.append(values[t]);
      stop.append(fmt::format("{:08X}", colors[t]));
      stops.append(stop);
    }
    result["stops"] = stops;
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

/*! \brief The producer, generation, file and content records of a message and the
    attributes of the message (the Info presentation). */

Json::Value Plugin::api_info(const HTTP::Request& theRequest)
{
  try
  {
    const auto fileId = static_cast<T::FileId>(requiredInteger(theRequest, "file"));
    const auto messageIndex = static_cast<T::MessageIndex>(requiredInteger(theRequest, "message"));
    auto contentServer = itsGridEngine->getContentServer_sptr();
    auto dataServer = itsGridEngine->getDataServer_sptr();

    T::ContentInfo info;
    if (contentServer->getContentInfo(0, fileId, messageIndex, info) != 0)
      throw BadRequest("Unknown file or message");

    T::FileInfo fileInfo;
    contentServer->getFileInfoById(0, fileId, fileInfo);
    T::GenerationInfo generationInfo;
    contentServer->getGenerationInfoById(0, info.mGenerationId, generationInfo);
    T::ProducerInfo producerInfo;
    contentServer->getProducerInfoById(0, info.mProducerId, producerInfo);

    Json::Value result(Json::objectValue);

    Json::Value producer(Json::objectValue);
    producer["Id"] = producerInfo.mProducerId;
    producer["Name"] = producerInfo.mName;
    producer["Title"] = producerInfo.mTitle;
    producer["Description"] = producerInfo.mDescription;
    result["Producer"] = producer;

    Json::Value generation(Json::objectValue);
    generation["Id"] = Json::UInt64(generationInfo.mGenerationId);
    generation["Name"] = generationInfo.mName;
    generation["Description"] = generationInfo.mDescription;
    generation["Analysis time"] = generationInfo.mAnalysisTime;
    generation["Ready"] = (generationInfo.mStatus == T::GenerationInfo::Status::Ready);
    if (generationInfo.mDeletionTime > 0)
      generation["Deletion time"] = utcTimeFromTimeT(generationInfo.mDeletionTime);
    result["Generation"] = generation;

    Json::Value file(Json::objectValue);
    file["Id"] = Json::UInt64(fileInfo.mFileId);
    file["Server"] = fileInfo.mServer;
    file["Protocol"] = fileInfo.mProtocol;
    file["Type"] = fileInfo.mFileType;
    file["Name"] = fileInfo.mName;
    if (fileInfo.mModificationTime > 0)
      file["Modification time"] = utcTimeFromTimeT(fileInfo.mModificationTime);
    if (fileInfo.mDeletionTime > 0)
      file["Deletion time"] = utcTimeFromTimeT(fileInfo.mDeletionTime);
    result["File"] = file;

    Json::Value content(Json::objectValue);
    content["Message index"] = info.mMessageIndex;
    content["File position"] = Json::UInt64(info.mFilePosition);
    content["Size"] = info.mMessageSize;
    content["Forecast time"] = info.getForecastTime();
    content["Level"] = info.mParameterLevel;
    content["FMI identifier"] = info.mFmiParameterId;
    content["FMI name"] = info.getFmiParameterName();
    content["FMI level identifier"] = info.mFmiParameterLevelId;
    content["Forecast type"] = info.mForecastType;
    content["Forecast number"] = info.mForecastNumber;
    content["Geometry identifier"] = info.mGeometryId;
    result["Content"] = content;

    T::AttributeList attributeList;
    Json::Value attributes(Json::arrayValue);
    if (dataServer->getGridAttributeList(0, fileId, messageIndex, attributeList) == 0)
    {
      const uint len = attributeList.getLength();
      for (uint t = 0; t < len; t++)
      {
        T::Attribute* attr = attributeList.getAttributeByIndex(t);
        Json::Value a(Json::arrayValue);
        a.append(attr->mName);
        a.append(attr->mValue);
        attributes.append(a);
      }
    }
    result["Attributes"] = attributes;
    return result;
  }
  catch (const BadRequest&)
  {
    throw;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace GridGui
}  // namespace Plugin
}  // namespace SmartMet
