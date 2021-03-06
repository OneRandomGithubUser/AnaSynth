#include <emscripten/val.h>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // uuid generators
#include <boost/functional/hash.hpp>      // uuid hashing for maps

// debug
#include <iostream>
#include <boost/uuid/uuid_io.hpp>         // uuid streaming operators

#include <numbers>
#include <cmath>
#include <vector>
#include <optional>
#include <random>
#include <vector>
#include <map>
#include <tuple>
#include <unordered_map>
#include <algorithm>

emscripten::val window = emscripten::val::global("window");
emscripten::val document = emscripten::val::global("document");
const double pi = std::numbers::pi;
const double e = std::numbers::e;
static int page;
bool circuitCompleted = false;
boost::uuids::random_generator uuidGenerator;
std::vector<boost::uuids::uuid> uuids;
static double resistance = 4, efficiency = 86.5, inductance = 0, capacitance = 0, frequency = 0, initialVolume = 0, timeConstant = 0, watts = 0, volts = 0, decibels = 0;
static bool playButtonEnabled = false;
static bool nextButtonEnabled = false;

static std::vector<bool> pianoKeys;
static std::vector<bool> previousKeys;
static std::vector<std::vector<boost::uuids::uuid>> pianouuids;

static const std::map<std::string, double> frequencyMap = {
  {"C4", 261.63},
  {"C#", 277.18},
  {"D", 293.66},
  {"D#", 311.13},
  {"E", 329.63},
  {"F", 349.23},
  {"F#", 369.99},
  {"G", 392.00},
  {"G#", 415.30},
  {"A", 440.00},
  {"A#", 466.16},
  {"B", 493.88},
  {"C5", 523.25}
};

static const double frequencyArray[13] = {261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00, 415.30, 440.00, 466.16, 493.88, 523.25};


void PlayOrPauseSound(emscripten::val event);

namespace audio
{
  // yes i know this should be done with OOP. but no time
  emscripten::val globalAudioContext = emscripten::val::global("AudioContext");
  // audioContext is allowed to start only after user interactions, so this must only be created when initialized
  std::optional<emscripten::val> audioContext;
  std::unordered_map<boost::uuids::uuid, emscripten::val, boost::hash<boost::uuids::uuid>> oscillators;
  std::unordered_map<boost::uuids::uuid, emscripten::val, boost::hash<boost::uuids::uuid>> gainNodes;
  std::unordered_map<boost::uuids::uuid, emscripten::val, boost::hash<boost::uuids::uuid>> volumeManagers;
  std::unordered_map<boost::uuids::uuid, double, boost::hash<boost::uuids::uuid>> timeConstants; // in seconds
  std::unordered_map<boost::uuids::uuid, double, boost::hash<boost::uuids::uuid>> beginTimes;
  std::unordered_map<boost::uuids::uuid, double, boost::hash<boost::uuids::uuid>> frequencies;
  std::unordered_map<boost::uuids::uuid, double, boost::hash<boost::uuids::uuid>> initialVolumes;
  std::unordered_map<boost::uuids::uuid, int, boost::hash<boost::uuids::uuid>> elapsedTimeConstants;
  bool initialized = false;
  bool playing = false;
  /*
  class rlc
  {
  private:
    double frequency;
    double timeConstant;
    double initialVolume;
    emscripten::val gainNode;
    emscripten::val volumeManager;
    emscripten::val oscillator;
    bool playing;
  public:
    rlc(double frequency, double timeConstant, double intialVolume)
    {
      this->frequency = frequency;
      this->timeConstant = timeConstant;
      this->initialVolume = initialVolume;
      gainNode = audio::audioContext->call<emscripten::val>("createGain");
      gainNode.call<void>("connect", audio::audioContext.value()["destination"]);
      oscillator = audioContext.value().call<emscripten::val>("createOscillator");
      oscillator.set("type", emscripten::val("sine"));
      oscillator["frequency"].set("value", emscripten::val(frequency));
      oscillators.emplace_back(oscillator);
      oscillator.call<void>("start");
      playing = false;
    }
    void play_or_pause()
    {
      if (playing)
      {
        // pause it
        playing = false;
      } else {
        // play it
        playing = true;
      }
    }
  };*/
  void volume_control()
  {
    if (audioContext.has_value())
    {
      for (auto& [uuid, gainNode] : gainNodes) {
        // time after play() in seconds. add one TIME_CONSTANT since this is the time at the end of the exponentialRampToValueAtTime
        double offsetTime = audioContext.value()["currentTime"].as<double>() - beginTimes.at(uuid);
        // rounding isn't very accurate but so is TimeInterval TODO
        if (round(offsetTime / timeConstants.at(uuid)) == elapsedTimeConstants.at(uuid)) {
          elapsedTimeConstants.at(uuid) = round(offsetTime / timeConstants.at(uuid)) + 1;
          if (pow(e, -elapsedTimeConstants.at(uuid)) >
              2 * pow(10, -45)) // can't exponentialRampToValueAtTime to 0 if timeConstants is too high
          {
            std::cout << offsetTime << " sec to " << offsetTime + timeConstants.at(uuid) << " sec: decrease to volume of "
                      << 100 * pow(e, -elapsedTimeConstants.at(uuid)) << "% (e^-" << elapsedTimeConstants.at(uuid) << ") from volume "
                      << gainNodes.at(uuid)["gain"]["value"].as<double>() << " at " << elapsedTimeConstants.at(uuid)
                      << " time constants\n";
            gainNode["gain"].call<void>("setValueAtTime", gainNode["gain"]["value"],
                                                audioContext.value()["currentTime"]);
            gainNode["gain"].call<emscripten::val>("exponentialRampToValueAtTime",
                                                           emscripten::val(initialVolumes.at(uuid) * pow(e, -elapsedTimeConstants.at(uuid))),
                                                           emscripten::val(beginTimes.at(uuid) + offsetTime + timeConstants.at(uuid)));
          }
        }
      }
    }
  }
  void play_or_stop_everything()
  {
    if (!initialized)
    {
      // This function can be called ONLY after audio::initialize() is called
      // emscripten currently does not have much support for throwing std::exception
      std::cout << "Error: audio::play() called before audio::initialize()\n";
    }
    emscripten::val currentTime = audioContext.value()["currentTime"];
    if (playing)
    {
      // stop
      for (auto& [uuid, oscillator] : oscillators) {
        gainNodes.at(uuid)["gain"].call<void>("cancelScheduledValues", currentTime);
        oscillators.at(uuid).call<void>("disconnect", gainNodes.at(uuid));
        if (volumeManagers.contains(uuid)) {
          window.call<void>("clearInterval", volumeManagers.at(uuid));
        }
        elapsedTimeConstants.at(uuid) = 0;
        playing = false;
      }
      volumeManagers.clear();
    } else {
      // play
      for (auto& [uuid, oscillator] : oscillators) {
        oscillators.at(uuid).call<void>("connect", gainNodes.at(uuid));
        beginTimes.at(uuid) = currentTime.as<double>();
        gainNodes.at(uuid)["gain"].set("value", emscripten::val(initialVolumes.at(uuid)));
        volume_control();
        volumeManagers.try_emplace(uuid,
                window.call<emscripten::val>("setInterval", emscripten::val::module_property("VolumeControl"),
                                             emscripten::val(timeConstants.at(uuid) * 1000)));
        playing = true;
      }
    }
  }
  void play_or_stop(std::vector<boost::uuids::uuid>& rlcUuids)
  {
    if (!initialized)
    {
      // This function can be called ONLY after audio::initialize() is called
      // emscripten currently does not have much support for throwing std::exception
      std::cout << "Error: audio::play() called before audio::initialize()\n";
    }
    emscripten::val currentTime = audioContext.value()["currentTime"];
    for (auto& uuid : rlcUuids) {
      if (volumeManagers.contains(uuid)) {
        // stop
        gainNodes.at(uuid)["gain"].call<void>("cancelScheduledValues", currentTime);
        oscillators.at(uuid).call<void>("disconnect", gainNodes.at(uuid));
        window.call<void>("clearInterval", volumeManagers.at(uuid));
        volumeManagers.erase(uuid);
        elapsedTimeConstants.at(uuid) = 0;
        playing = false;
      } else {
        // play
        oscillators.at(uuid).call<void>("connect", gainNodes.at(uuid));
        beginTimes.at(uuid) = audioContext.value()["currentTime"].as<double>();
        gainNodes.at(uuid)["gain"].set("value", emscripten::val(initialVolumes.at(uuid)));
        volume_control();
        volumeManagers.try_emplace(uuid,
                                   window.call<emscripten::val>("setInterval",
                                                                emscripten::val::module_property("VolumeControl"),
                                                                emscripten::val(timeConstants.at(uuid) * 1000)));
        playing = true;
      }
    }
  }
  void add_rlcs(std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>>& frequenciesStartingVolumesTimeConstants)
  {
    if (initialized)
    {
      emscripten::val time = audioContext.value()["currentTime"];
      for (auto& [uuid, tuple] : frequenciesStartingVolumesTimeConstants)
      {
        auto [frequency, startingVolume, timeConstant] = tuple;
        emscripten::val oscillator = audioContext.value().call<emscripten::val>("createOscillator");
        oscillator.set("type", emscripten::val("sine"));
        oscillator["frequency"].set("value", emscripten::val(frequency));
        oscillators.try_emplace(uuid, oscillator);
        emscripten::val gainNode = audioContext.value().call<emscripten::val>("createGain");
        gainNodes.try_emplace(uuid, gainNode);
        gainNode.call<void>("connect", audioContext.value()["destination"]);
        timeConstants.try_emplace(uuid, timeConstant);
        frequencies.try_emplace(uuid, frequency);
        initialVolumes.try_emplace(uuid, startingVolume);
        elapsedTimeConstants.try_emplace(uuid, 0);
      }
      double currentTime = audioContext.value()["currentTime"].as<double>();
      for (auto& [uuid, tuple] : frequenciesStartingVolumesTimeConstants) {
        beginTimes.try_emplace(uuid, currentTime);
        oscillators.at(uuid).call<void>("start");
      }
    }
  }
  std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> remove_rlcs(std::vector<boost::uuids::uuid>& rlcUuids)
  {
    std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> ans;
    for (auto& uuid : rlcUuids) {
      ans.try_emplace(uuid, std::make_tuple(frequencies.at(uuid), initialVolumes.at(uuid), timeConstants.at(uuid)));
      if (volumeManagers.contains(uuid))
      {
        oscillators.at(uuid).call<void>("disconnect", gainNodes.at(uuid));
        window.call<void>("clearInterval", volumeManagers.at(uuid));
        volumeManagers.erase(uuid);
      }
      oscillators.at(uuid).call<void>("stop");
      oscillators.erase(uuid);
      gainNodes.at(uuid).call<void>("disconnect");
      gainNodes.erase(uuid);
      timeConstants.erase(uuid);
      beginTimes.erase(uuid);
      frequencies.erase(uuid);
      initialVolumes.erase(uuid);
      elapsedTimeConstants.erase(uuid);
    }
    return ans;
  }
  void remove_all_rlcs()
  {
    for (auto& [uuid, volumeManager] : volumeManagers)
    {
      oscillators.at(uuid).call<void>("disconnect", gainNodes.at(uuid));
      window.call<void>("clearInterval", volumeManagers.at(uuid));
    }
    volumeManagers.clear();
    for (auto& [uuid, oscillator] : oscillators)
    {
      oscillator.call<void>("stop");
      gainNodes.at(uuid).call<void>("disconnect");
    }
    oscillators.clear();
    gainNodes.clear();
    timeConstants.clear();
    beginTimes.clear();
    frequencies.clear();
    initialVolumes.clear();
    elapsedTimeConstants.clear();
  }
  bool get_playing()
  {
    return playing;
  }
  bool get_rlc_playing(boost::uuids::uuid uuid)
  {
    return volumeManagers.contains(uuid);
  }
  std::unordered_map<boost::uuids::uuid, double, boost::hash<boost::uuids::uuid>>& get_frequencies()
  {
    return frequencies;
  }
  std::unordered_map<boost::uuids::uuid, double, boost::hash<boost::uuids::uuid>>& get_initial_volumes()
  {
    return initialVolumes;
  }
  std::unordered_map<boost::uuids::uuid, double, boost::hash<boost::uuids::uuid>>& get_time_constants()
  {
    return timeConstants;
  }
  double get_current_volume(boost::uuids::uuid uuid)
  {
    if (volumeManagers.contains(uuid)) {
      return initialVolumes.at(uuid) * pow(e, -((audioContext.value()["currentTime"].as<double>() - beginTimes.at(uuid)) / timeConstants.at(uuid)));
    } else {
      return 0;
    }
  }
  double get_current()
  {
    double temp = 0.0;
    for (auto& [uuid, volumeManager] : volumeManagers)
    {
      temp += get_current_volume(uuid) * sin(2*pi*frequencies.at(uuid)*(audioContext.value()["currentTime"].as<double>() - beginTimes.at(uuid)));
    }
    return temp;
  }
  double get_slowed_current()
  {
    double temp = 0.0;
    for (auto& [uuid, volumeManager] : volumeManagers)
    {
      temp += get_current_volume(uuid) * sin((2*pi*frequencies.at(uuid)*audioContext.value()["currentTime"].as<double>() - beginTimes.at(uuid))/100);
    }
    return temp;
  }
  double get_example_current()
  {
    // always playes a 440 Hz sound at a volume of 1 and slows it down by a factor of 1,000 (performace.now() is in milliseconds)
    return sin(2*pi*440*(emscripten::val::global("performance").call<emscripten::val>("now").as<double>()/1000)/1000);
  }
  double get_example_rc_current()
  {
    // always plays a 440 Hz sound at a volume of 1 and slows it down by a time constant of 5.0 seconds
    double currentTime = emscripten::val::global("performance").call<emscripten::val>("now").as<double>()/1000;
    double cycle = fmod(currentTime, 5.0);
    if (cycle > 4.0)
    {
       return 0;
    } else {
      return sin(2*pi*440*(currentTime)) * pow(e, -cycle);
    }
  }
  double get_slowed_example_rc_current()
  {
    // always plays a 440 Hz sound at a volume of 1 and slows it down by a time constant of 5.0 seconds
    double currentTime = emscripten::val::global("performance").call<emscripten::val>("now").as<double>()/1000;
    double cycle = fmod(currentTime, 5.0);
    if (cycle > 4.0)
    {
      return 0;
    } else {
      return sin(2*pi*440*(currentTime/100)) * pow(e, -cycle);
    }
  }
  void initialize()
  {
    // because you cannot create audioContext until user interaction with the page, a rule enforced by browsers
    if (!initialized)
    {
      emscripten::val baseAudioContext = globalAudioContext.new_();
      audioContext.emplace(baseAudioContext);
      initialized = true;
    }
  }
  double watts_to_decibels(double power, double distance)
  {
    // assumes Normal Temperature and Pressure, aka 20 degrees Celsius,	101.325 kPa
    // here, air density is 1.2923 kg/m^3 and the speed of sound is 343 m/s
    // assumes reference sound pressure (human hearing threshold) is 20 ??Pa
    // also assumes the listener's eardrums are perfectly perpendicular to the sound
    double soundPressure = sqrt((power*1.2923*343)/(4*pi*distance*distance));
    return 20*log10(soundPressure/(20*pow(10,-6)));
  }
  double decibels_to_watts(double soundPressureLevel, double distance)
  {
    // assumes Normal Temperature and Pressure, aka 20 degrees Celsius,	101.325 kPa
    // here, air density is 1.2923 kg/m^3 and the speed of sound is 343 m/s
    // assumes reference sound pressure (human hearing threshold) is 20 ??Pa
    // also assumes the listener's eardrums are perfectly perpendicular to the sound
    double soundPressure = pow(10, soundPressureLevel/20) * 20*pow(10,-6);
    return (4*pi*distance*distance*pow(soundPressure,2))/(1.2923*343);
  }
}

void PlayOrPauseSound(emscripten::val event)
{
  audio::play_or_stop_everything();
  emscripten::val play = document.call<emscripten::val>("getElementById", emscripten::val("play"));
  if(audio::get_playing()) {
    play.set("innerHTML", "PAUSE");
  } else {
    play.set("innerHTML", "PLAY");
  }
}

void InteractWithCanvas(emscripten::val event)
{
  // std::string eventName = event["type"].as<std::string>();
  // double pageX = event["pageX"].as<double>();
  // double pageY = event["pageY"].as<double>();
  // std::cout << eventName << " " << pageX << " " << pageY << "\n";
}

void InteractWithKeyboard(emscripten::val event)
{
  switch(page) {
    case(11):
    {
      std::string eventName = event["type"].as<std::string>();
      if(eventName == "keydown") {
        switch(event["keyCode"].as<int>()) {
          case 90:
            pianoKeys.at(0) = true;
            break;
          case 83:
            pianoKeys.at(1) = true;
            break;
          case 88:
            pianoKeys.at(2) = true;
            break;
          case 68:
            pianoKeys.at(3) = true;
            break;
          case 67:
            pianoKeys.at(4) = true;
            break;
          case 86:
            pianoKeys.at(5) = true;
            break;
          case 71:
            pianoKeys.at(6) = true;
            break;
          case 66:
            pianoKeys.at(7) = true;
            break;
          case 72:
            pianoKeys.at(8) = true;
            break;
          case 78:
            pianoKeys.at(9) = true;
            break;
          case 74:
            pianoKeys.at(10) = true;
            break;
          case 77:
            pianoKeys.at(11) = true;
            break;
          case 188:
            pianoKeys.at(12) = true;
            break;
          default:
            break;
        }
      } else if (eventName == "keyup") {
        switch(event["keyCode"].as<int>()) {
          case 90:
            pianoKeys.at(0) = false;
            break;
          case 83:
            pianoKeys.at(1) = false;
            break;
          case 88:
            pianoKeys.at(2) = false;
            break;
          case 68:
            pianoKeys.at(3) = false;
            break;
          case 67:
            pianoKeys.at(4) = false;
            break;
          case 86:
            pianoKeys.at(5) = false;
            break;
          case 71:
            pianoKeys.at(6) = false;
            break;
          case 66:
            pianoKeys.at(7) = false;
            break;
          case 72:
            pianoKeys.at(8) = false;
            break;
          case 78:
            pianoKeys.at(9) = false;
            break;
          case 74:
            pianoKeys.at(10) = false;
            break;
          case 77:
            pianoKeys.at(11) = false;
            break;
          case 188:
            pianoKeys.at(12) = false;
            break;
          default:
            break;
        }
      }
      break;
    }
    default:
      break;
  }
}

void ResizeCanvas(emscripten::val event)
{
  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  canvas.set("width", emscripten::val(window["innerWidth"].as<double>() * 0.7));
  canvas.set("height", emscripten::val(window["innerHeight"].as<double>() - 80));
  ctx.set("textAlign", emscripten::val("center"));
  ctx.set("textBaseline", emscripten::val("middle"));
  ctx.set("font", emscripten::val("20px Calibri"));
}

// width: 60
void DrawResistor(emscripten::val ctx, int x, int y, bool highlight) {
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("#00BFFF"));
    ctx.set("lineWidth", emscripten::val(3));
  }
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x-30, y);
  ctx.call<void>("lineTo", x-25, y-8);
  ctx.call<void>("lineTo", x-15, y+8);
  ctx.call<void>("lineTo", x-5, y-8);
  ctx.call<void>("lineTo", x+5, y+8);
  ctx.call<void>("lineTo", x+15, y-8);
  ctx.call<void>("lineTo", x+25, y+8);
  ctx.call<void>("lineTo", x+30, y);
  ctx.call<void>("stroke");
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("black"));
    ctx.set("lineWidth", emscripten::val(1));
  }
}

// width: 50
void DrawCapacitor(emscripten::val ctx, int x, int y, bool highlight) {
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("#00BFFF"));
    ctx.set("lineWidth", emscripten::val(3));
  }
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x-25, y);
  ctx.call<void>("lineTo", x-5, y);
  ctx.call<void>("moveTo", x-5, y+20);
  ctx.call<void>("lineTo", x-5, y-20);
  ctx.call<void>("moveTo", x+5, y+20);
  ctx.call<void>("lineTo", x+5, y-20);
  ctx.call<void>("moveTo", x+5, y);
  ctx.call<void>("lineTo", x+25, y);
  ctx.call<void>("stroke");
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("black"));
    ctx.set("lineWidth", emscripten::val(1));
  }
}

// width: 60
void DrawInductor(emscripten::val ctx, int x, int y, bool highlight) {
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("#00BFFF"));
    ctx.set("lineWidth", emscripten::val(3));
  }
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x-40, y);
  ctx.call<void>("arc", x-20, y, 10, pi, 0, false);
  ctx.call<void>("arc", x, y, 10, pi, 0, false);
  ctx.call<void>("arc", x+20, y, 10, pi, 0, false);
  ctx.call<void>("lineTo", x+40, y);
  ctx.call<void>("stroke");
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("Black"));
    ctx.set("lineWidth", emscripten::val(1));
  }
}

void DrawSpeaker(emscripten::val ctx, int x, int y, bool highlight) {
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("#00BFFF"));
    ctx.set("lineWidth", emscripten::val(3));
  }
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x-10, y-5);
  ctx.call<void>("lineTo", x-10, y+5);
  ctx.call<void>("lineTo", x+10, y+5);
  ctx.call<void>("lineTo", x+10, y-5);
  ctx.call<void>("lineTo", x-10, y-5);
  ctx.call<void>("lineTo", x-10, y-5);
  ctx.call<void>("lineTo", x-20, y-20);
  ctx.call<void>("lineTo", x+20, y-20);
  ctx.call<void>("lineTo", x+10, y-5);
  ctx.call<void>("stroke");
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("Black"));
    ctx.set("lineWidth", emscripten::val(1));
  }
}

void DrawBattery(emscripten::val ctx, int x, int y, bool highlight) {
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("#00BFFF"));
    ctx.set("lineWidth", emscripten::val(3));
  }
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x-25, y);
  ctx.call<void>("lineTo", x-15, y);
  ctx.call<void>("moveTo", x-15, y+10);
  ctx.call<void>("lineTo", x-15, y-10);
  ctx.call<void>("moveTo", x-5, y+20);
  ctx.call<void>("lineTo", x-5, y-20);
  ctx.call<void>("moveTo", x+5, y+10);
  ctx.call<void>("lineTo", x+5, y-10);
  ctx.call<void>("moveTo", x+15, y+20);
  ctx.call<void>("lineTo", x+15, y-20);
  ctx.call<void>("moveTo", x+15, y);
  ctx.call<void>("lineTo", x+25, y);
  ctx.call<void>("stroke");
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("Black"));
    ctx.set("lineWidth", emscripten::val(1));
  }
}
void DrawCurrent(emscripten::val ctx, double x, double y, double spacing, double arrowLength, std::string label, bool highlight)
{
  // NOTE: this must be placed on a TOP edge, also this assumes 60 fps TODO
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("#00BFFF"));
    ctx.set("lineWidth", emscripten::val(3));
  }
  double labelDescent = ctx.call<emscripten::val>("measureText", emscripten::val(label))["actualBoundingBoxDescent"].as<double>();
  double labelAscent = ctx.call<emscripten::val>("measureText", emscripten::val(label))["actualBoundingBoxAscent"].as<double>();
  ctx.call<void>("fillText", emscripten::val(label), x, y - spacing - labelDescent);
  ctx.call<void>("fillText", emscripten::val("CURRENT"), x, y - spacing - labelDescent - labelAscent - spacing -
                                                            ctx.call<emscripten::val>("measureText",
                                                                                      emscripten::val(
                                                                                              "CURRENT"))["actualBoundingBoxDescent"].as<double>());

  ctx.call<void>("beginPath");
  if (arrowLength > 0)
  {
    ctx.call<void>("moveTo", x, y + spacing);
    ctx.call<void>("lineTo", x + arrowLength, y + spacing);
    ctx.call<void>("moveTo", x + arrowLength - spacing/2.0, y + spacing/2.0);
    ctx.call<void>("lineTo", x + arrowLength, y + spacing);
    ctx.call<void>("lineTo", x + arrowLength - spacing/2.0, y + 3*spacing/2.0);
  } else if (arrowLength < 0) {
    ctx.call<void>("moveTo", x, y + spacing);
    ctx.call<void>("lineTo", x + arrowLength, y + spacing);
    ctx.call<void>("moveTo", x + arrowLength + spacing/2.0, y + spacing/2.0);
    ctx.call<void>("lineTo", x + arrowLength, y + spacing);
    ctx.call<void>("lineTo", x + arrowLength + spacing/2.0, y + 3*spacing/2.0);
  } else {
    ctx.call<void>("fillText", emscripten::val("0"), x, y + spacing + ctx.call<emscripten::val>("measureText",
                                                                                                emscripten::val(
                                                                                                        "0"))["actualBoundingBoxAscent"].as<double>());
  }
  ctx.call<void>("stroke");
  if (highlight)
  {
    ctx.set("strokeStyle", emscripten::val("Black"));
    ctx.set("lineWidth", emscripten::val(1));
  }
}
void DrawExampleCircuit(emscripten::val ctx, bool highlightCapacitor, bool highlightInductor, bool highlightResistor, bool highlightBattery) {
  double width = ctx["canvas"]["width"].as<double>();
  double height = ctx["canvas"]["height"].as<double>();
  ctx.set("fillStyle", emscripten::val("black"));
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.5, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.5-20, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+45, height*0.5, 2, 0, 2*pi);
  ctx.call<void>("fill");
  DrawCapacitor(ctx, width*0.3, height*0.5, highlightCapacitor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25+20, height*0.5);
  ctx.call<void>("lineTo", width*0.7-40, height*0.5);
  ctx.call<void>("stroke");
  DrawInductor(ctx, width*0.7, height*0.5, highlightInductor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.7+39, height*0.5);
  ctx.call<void>("lineTo", width*0.9, height*0.5);
  ctx.call<void>("lineTo", width*0.9, height*0.2);
  ctx.call<void>("lineTo", width*0.3+30, height*0.2);
  ctx.call<void>("stroke");
  DrawResistor(ctx, width*0.3, height*0.2, highlightResistor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3-30, height*0.2);
  ctx.call<void>("lineTo", width*0.1, height*0.2);
  ctx.call<void>("lineTo", width*0.1, height*0.5);
  ctx.call<void>("lineTo", width*0.3-25, height*0.5);
  ctx.call<void>("lineTo", width*0.3-25, height*0.4);
  ctx.call<void>("stroke");
  DrawBattery(ctx, width*0.3, height*0.4, highlightBattery);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.4);
  ctx.call<void>("lineTo", width*0.3+25, height*0.5-20);
  ctx.call<void>("stroke");
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.5);
  double current = audio::get_example_rc_current();
  if (current != 0) {
    ctx.call<void>("lineTo", width * 0.3 + 25 + 20, height * 0.5);
  } else {
    ctx.call<void>("lineTo", width * 0.3 + 25, height * 0.5 - 20);
  }
  ctx.call<void>("stroke");
  DrawCurrent(ctx, width * 0.5, height * 0.2, 10, width * 0.1 * audio::get_slowed_example_rc_current(), "(SLOWED 100x)", false);
  DrawCurrent(ctx, width * 0.7, height * 0.2, 10, width * 0.1 * current, "(REAL TIME)", false);
}

void DrawFullCircuit(emscripten::val ctx, bool highlightCapacitor, bool highlightInductor, bool highlightSpeaker, bool highlightBattery) {
  double width = ctx["canvas"]["width"].as<double>();
  double height = ctx["canvas"]["height"].as<double>();
  DrawCapacitor(ctx, width*0.3, height*0.5, highlightCapacitor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25+20, height*0.5);
  ctx.call<void>("lineTo", width*0.7-40, height*0.5);
  ctx.call<void>("stroke");
  DrawInductor(ctx, width*0.7, height*0.5, highlightInductor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.7+39, height*0.5);
  ctx.call<void>("lineTo", width*0.9, height*0.5);
  ctx.call<void>("lineTo", width*0.9, height*0.2);
  ctx.call<void>("lineTo", width*0.3+10, height*0.2);
  ctx.call<void>("stroke");
  DrawSpeaker(ctx, width*0.3, height*0.2, highlightSpeaker);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3-12, height*0.2);
  ctx.call<void>("lineTo", width*0.1, height*0.2);
  ctx.call<void>("lineTo", width*0.1, height*0.5);
  ctx.call<void>("lineTo", width*0.3-25, height*0.5);
  ctx.call<void>("lineTo", width*0.3-25, height*0.4);
  ctx.call<void>("stroke");
  DrawBattery(ctx, width*0.3, height*0.4, highlightBattery);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.4);
  ctx.call<void>("lineTo", width*0.3+25, height*0.5-20);
  ctx.call<void>("stroke");
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.5);
  if (audio::get_playing()) {
    ctx.call<void>("lineTo", width * 0.3 + 25 + 20, height * 0.5);
  } else {
    ctx.call<void>("lineTo", width * 0.3 + 25, height * 0.5 - 20);
  }
  ctx.call<void>("stroke");
  ctx.set("fillStyle", emscripten::val("black"));
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.5, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.5-20, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+45, height*0.5, 2, 0, 2*pi);
  ctx.call<void>("fill");
}

void DrawTwoCircuits(emscripten::val ctx, bool highlightCapacitor, bool highlightInductor, bool highlightSpeaker, bool highlightBattery) {
  double width = ctx["canvas"]["width"].as<double>();
  double height = ctx["canvas"]["height"].as<double>();
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.7+39, height*0.5);
  ctx.call<void>("lineTo", width*0.9, height*0.5);
  ctx.call<void>("lineTo", width*0.9, height*0.2);
  ctx.call<void>("lineTo", width*0.3+10, height*0.2);
  ctx.call<void>("stroke");
  DrawSpeaker(ctx, width*0.3, height*0.2, highlightSpeaker);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3-12, height*0.2);
  ctx.call<void>("lineTo", width*0.1, height*0.2);
  ctx.call<void>("lineTo", width*0.1, height*0.5);
  ctx.call<void>("lineTo", width*0.3-25, height*0.5);
  ctx.call<void>("lineTo", width*0.3-25, height*0.4);
  ctx.call<void>("stroke");
  DrawBattery(ctx, width*0.3, height*0.4, highlightBattery);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.4);
  ctx.call<void>("lineTo", width*0.3+25, height*0.5-20);
  ctx.call<void>("stroke");
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.5);
  if (audio::get_playing()) {
    ctx.call<void>("lineTo", width * 0.3 + 25 + 20, height * 0.5);
  } else {
    ctx.call<void>("lineTo", width * 0.3 + 25, height * 0.5 - 20);
  }
  ctx.call<void>("stroke");
  DrawCapacitor(ctx, width*0.3, height*0.5, highlightCapacitor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25+20, height*0.5);
  ctx.call<void>("lineTo", width*0.7-40, height*0.5);
  ctx.call<void>("stroke");
  DrawInductor(ctx, width*0.7, height*0.5, highlightInductor);
  ctx.set("fillStyle", emscripten::val("black"));
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.5, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.5-20, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+45, height*0.5, 2, 0, 2*pi);
  ctx.call<void>("fill");

  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.1, height*0.5);
  ctx.call<void>("lineTo", width*0.1, height*0.8);
  ctx.call<void>("lineTo", width*0.3-25, height*0.8);
  ctx.call<void>("lineTo", width*0.3-25, height*0.7);
  ctx.call<void>("stroke");
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.7+39, height*0.8);
  ctx.call<void>("lineTo", width*0.9, height*0.8);
  ctx.call<void>("lineTo", width*0.9, height*0.5);
  ctx.call<void>("stroke");
  DrawBattery(ctx, width*0.3, height*0.7, highlightBattery);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.7);
  ctx.call<void>("lineTo", width*0.3+25, height*0.8-20);
  ctx.call<void>("stroke");
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.8);
  if (audio::get_playing()) {
    ctx.call<void>("lineTo", width * 0.3 + 25 + 20, height * 0.8);
  } else {
    ctx.call<void>("lineTo", width * 0.3 + 25, height * 0.8 - 20);
  }
  ctx.call<void>("stroke");
  DrawCapacitor(ctx, width*0.3, height*0.8, highlightCapacitor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25+20, height*0.8);
  ctx.call<void>("lineTo", width*0.7-40, height*0.8);
  ctx.call<void>("stroke");
  DrawInductor(ctx, width*0.7, height*0.8, highlightInductor);
  ctx.set("fillStyle", emscripten::val("black"));
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.8, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.8-20, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+45, height*0.8, 2, 0, 2*pi);
  ctx.call<void>("fill");
}

void DrawFourierCircuit(emscripten::val ctx, bool highlightCapacitor, bool highlightInductor, bool highlightSpeaker, bool highlightBattery) {
  double width = ctx["canvas"]["width"].as<double>();
  double height = ctx["canvas"]["height"].as<double>();
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.7+39, height*0.3);
  ctx.call<void>("lineTo", width*0.9, height*0.3);
  ctx.call<void>("lineTo", width*0.9, height*0.1);
  ctx.call<void>("lineTo", width*0.3+10, height*0.1);
  ctx.call<void>("stroke");
  DrawSpeaker(ctx, width*0.3, height*0.1, highlightSpeaker);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3-12, height*0.1);
  ctx.call<void>("lineTo", width*0.1, height*0.1);
  ctx.call<void>("lineTo", width*0.1, height*0.3);
  ctx.call<void>("lineTo", width*0.3-25, height*0.3);
  ctx.call<void>("lineTo", width*0.3-25, height*0.2);
  ctx.call<void>("stroke");
  DrawBattery(ctx, width*0.3, height*0.2, highlightBattery);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.2);
  ctx.call<void>("lineTo", width*0.3+25, height*0.3-20);
  ctx.call<void>("stroke");
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25, height*0.3);
  if (audio::get_playing()) {
    ctx.call<void>("lineTo", width * 0.3 + 25 + 20, height * 0.3);
  } else {
    ctx.call<void>("lineTo", width * 0.3 + 25, height * 0.3 - 20);
  }
  ctx.call<void>("stroke");
  DrawCapacitor(ctx, width*0.3, height*0.3, highlightCapacitor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25+20, height*0.3);
  ctx.call<void>("lineTo", width*0.7-40, height*0.3);
  ctx.call<void>("stroke");
  DrawInductor(ctx, width*0.7, height*0.3, highlightInductor);
  ctx.set("fillStyle", emscripten::val("black"));
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.3, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+25, height*0.3-20, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", width*0.3+45, height*0.3, 2, 0, 2*pi);
  ctx.call<void>("fill");

  for(int i = 0; i < 3; i++) {
    ctx.call<void>("beginPath");
    ctx.call<void>("moveTo", width*0.1, height*(0.3 + (0.2 * i)));
    ctx.call<void>("lineTo", width*0.1, height*(0.5 + (0.2 * i)));
    ctx.call<void>("lineTo", width*0.3-25, height*(0.5 + (0.2 * i)));
    ctx.call<void>("lineTo", width*0.3-25, height*(0.4 + (0.2 * i)));
    ctx.call<void>("stroke");
    ctx.call<void>("beginPath");
    ctx.call<void>("moveTo", width*0.7+39, height*(0.5 + (0.2 * i)));
    ctx.call<void>("lineTo", width*0.9, height*(0.5 + (0.2 * i)));
    ctx.call<void>("lineTo", width*0.9, height*(0.3 + (0.2 * i)));
    ctx.call<void>("stroke");
    DrawBattery(ctx, width*0.3, height*(0.4 + (0.2 * i)), highlightBattery);
    ctx.call<void>("beginPath");
    ctx.call<void>("moveTo", width*0.3+25, height*(0.4 + (0.2 * i)));
    ctx.call<void>("lineTo", width*0.3+25, height*(0.5 + (0.2 * i)) - 20);
    ctx.call<void>("stroke");
    ctx.call<void>("beginPath");
    ctx.call<void>("moveTo", width*0.3+25, height*(0.5 + (0.2 * i)));
    if (audio::get_playing()) {
      ctx.call<void>("lineTo", width * 0.3 + 25 + 20, height * (0.5 + (0.2 * i)));
    } else {
      ctx.call<void>("lineTo", width * 0.3 + 25, height * (0.5 + (0.2 * i)) - 20);
    }
    ctx.call<void>("stroke");
    DrawCapacitor(ctx, width*0.3, height*(0.5 + (0.2 * i)), highlightCapacitor);
    ctx.call<void>("beginPath");
    ctx.call<void>("moveTo", width*0.3+25+20, height*(0.5 + (0.2 * i)));
    ctx.call<void>("lineTo", width*0.7-40, height*(0.5 + (0.2 * i)));
    ctx.call<void>("stroke");
    DrawInductor(ctx, width*0.7, height*(0.5 + (0.2 * i)), highlightInductor);
    ctx.set("fillStyle", emscripten::val("black"));
    ctx.call<void>("beginPath");
    ctx.call<void>("arc", width*0.3+25, height*(0.5 + (0.2 * i)), 2, 0, 2*pi);
    ctx.call<void>("fill");
    ctx.call<void>("beginPath");
    ctx.call<void>("arc", width*0.3+25, height*(0.5 + (0.2 * i))-20, 2, 0, 2*pi);
    ctx.call<void>("fill");
    ctx.call<void>("beginPath");
    ctx.call<void>("arc", width*0.3+45, height*(0.5 + (0.2 * i)), 2, 0, 2*pi);
    ctx.call<void>("fill");

    
    ctx.call<void>("beginPath");
    ctx.call<void>("arc", width*0.5, height*0.93, 2, 0, 2*pi);
    ctx.call<void>("fill");
    ctx.call<void>("beginPath");
    ctx.call<void>("arc", width*0.5, height*0.95, 2, 0, 2*pi);
    ctx.call<void>("fill");
    ctx.call<void>("beginPath");
    ctx.call<void>("arc", width*0.5, height*0.97, 2, 0, 2*pi);
    ctx.call<void>("fill");
    ctx.set("font", emscripten::val("15px Calibri"));
    ctx.call<void>("fillText", emscripten::val("(10 is probably good enough)"), width*0.5+110, height*0.95);
  }
}

int split(int input, int totalDistance)
{
  if (input < totalDistance / 3)
  {
    return -input;
  } else if (input < 2 * totalDistance / 3) {} {
    return totalDistance + input;
  }
}
int interpolate_split(std::vector<std::vector<int>>& interpolation, int index, int FRAME_COUNT, int totalDistance)
{
  return round((1 - (FRAME_COUNT % 300) / 300.0) * split(interpolation[0][index], totalDistance) +
                 ((FRAME_COUNT % 300) / 300.0) * split(interpolation[1][index], totalDistance));
}
int interpolate(std::vector<std::vector<int>>& interpolation, int index, int FRAME_COUNT)
{
  return round((1 - (FRAME_COUNT % 300) / 300.0) * interpolation[0][index] +
               ((FRAME_COUNT % 300) / 300.0) * interpolation[1][index]);
}
void RenderCanvas()
{
  static int FRAME_COUNT = 0;
  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));

  // subtly change the fillStyle color
  const static std::string hex[16] = {"0","1","2","3","4","5","6","7","8","9","A","B","C","D","E","F"};
  std::random_device rd;
  std::default_random_engine gen(rd());
  std::uniform_int_distribution<int> colorDist(200,255);
  std::uniform_int_distribution<int> widthDist(0,canvas["width"].as<int>());
  std::uniform_int_distribution<int> heightDist(0,canvas["height"].as<int>());
  static std::vector<std::vector<int>> interpolation;
  // two interpolation points over time, then r1, g1, b1, r2, g2, b2, startX, startY, endX, endY for the linear gradient
  if (FRAME_COUNT == 0) {
    interpolation.push_back({colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), widthDist(gen), heightDist(gen),
                                       widthDist(gen), heightDist(gen)});
    interpolation.push_back({colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), widthDist(gen), heightDist(gen),
                             widthDist(gen), heightDist(gen)});
  }
  if (FRAME_COUNT % 300 == 0) {
    interpolation.erase(interpolation.begin());
    interpolation.push_back({colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), colorDist(gen), widthDist(gen), heightDist(gen),
                                       widthDist(gen), heightDist(gen)});
  }
  emscripten::val gradient = ctx.call<emscripten::val>("createLinearGradient",
                                                       emscripten::val(interpolate_split(interpolation,6,FRAME_COUNT,canvas["width"].as<int>())),
                                                       emscripten::val(interpolate_split(interpolation,7,FRAME_COUNT,canvas["height"].as<int>())),
                                                       emscripten::val(interpolate_split(interpolation,8,FRAME_COUNT,canvas["width"].as<int>())),
                                                       emscripten::val(interpolate_split(interpolation,9,FRAME_COUNT,canvas["height"].as<int>())));
  std::string temp = "";
  int tens = 256*256*interpolate(interpolation,0,FRAME_COUNT)+256*interpolate(interpolation,1,FRAME_COUNT)+interpolate(interpolation,2,FRAME_COUNT);
  while (tens != 0)
  {
    temp = hex[tens%16] + temp;
    tens = floor(tens/16);
  }
  gradient.call<void>("addColorStop", emscripten::val(0), emscripten::val("#" + temp));
  temp = "";
  tens = 256*256*interpolate(interpolation,3,FRAME_COUNT)+256*interpolate(interpolation,4,FRAME_COUNT)+interpolate(interpolation,5,FRAME_COUNT);
  while (tens != 0)
  {
    temp = hex[tens%16] + temp;
    tens = floor(tens/16);
  }
  gradient.call<void>("addColorStop", emscripten::val(1), emscripten::val("#" + temp));
  ctx.set("fillStyle", gradient);
  ctx.call<void>("fillRect", 0, 0, canvas["width"], canvas["height"]);
  ctx.set("fillStyle", emscripten::val("black"));

  double width = canvas["width"].as<double>();
  double height = canvas["height"].as<double>();
  static int previewPage = 5;
  int tmp;
  if (!audio::initialized) {
    tmp = page;
    page = previewPage;
    if (FRAME_COUNT % 120 == 0) {
      std::uniform_int_distribution<int> pageDist(0,11);
      previewPage = pageDist(gen);
    }
  }
  switch(page)
  {
    case 0:
      DrawCapacitor(ctx, 195, 400, true);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 220, 400);
      ctx.call<void>("lineTo", 300, 400);
      ctx.call<void>("stroke");
      DrawInductor(ctx, 340, 400, true);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 380, 400);
      ctx.call<void>("lineTo", 430, 400);
      ctx.call<void>("lineTo", 430, 200);
      ctx.call<void>("lineTo", 130, 200);
      ctx.call<void>("lineTo", 130, 400);
      ctx.call<void>("lineTo", 170, 400);
      ctx.call<void>("stroke");
      DrawCurrent(ctx, 280, 200, 10, 150 * audio::get_example_current(), "(SLOWED 1000x)", false);
      break;
    case 1:
      DrawExampleCircuit(ctx, false, false, true, false);
      break;
    case 2:
      DrawFullCircuit(ctx, false, true, true, false);
      break;
    case 3:
      DrawFullCircuit(ctx, true, true, false, false);
      break;
    case 4:
      DrawFullCircuit(ctx, true, true, false, false);
      break;
    case 5: {
      // these are in pixels
      static int thickness = 30;
      static int centralThickness = 60;
      static int solenoidSpacing = 4;
      static int solenoidThickness = 80;
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", width * 0.2, height * 0.5 - width * 0.15);
      ctx.call<void>("lineTo", width * 0.2, height * 0.5 + width * 0.15);
      ctx.call<void>("lineTo", width * 0.8, height * 0.5 + width * 0.15);
      ctx.call<void>("lineTo", width * 0.8, height * 0.5 - width * 0.15);
      ctx.call<void>("closePath");
      ctx.call<void>("stroke");
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", width * 0.2, height * 0.5 - width * 0.15);
      ctx.call<void>("lineTo", 0, height * 0.5 - width * 0.35);
      ctx.call<void>("moveTo", width * 0.8, height * 0.5 - width * 0.15);
      ctx.call<void>("lineTo", width, height * 0.5 - width * 0.35);
      ctx.call<void>("moveTo", width * 0.2, height * 0.5);
      ctx.call<void>("lineTo", 0, height * 0.5);
      ctx.call<void>("moveTo", width * 0.8, height * 0.5);
      ctx.call<void>("lineTo", width, height * 0.5);
      ctx.call<void>("stroke");
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", width * 0.5 - centralThickness/2.0, height * 0.5 - thickness/2);
      ctx.call<void>("lineTo", width * 0.5 - centralThickness/2.0, height * 0.5 + width * 0.15 - thickness*2);
      ctx.call<void>("lineTo", width * 0.2 + thickness*2.0, height * 0.5 + width * 0.15 - thickness*2);
      ctx.call<void>("lineTo", width * 0.2 + thickness*2.0, height * 0.5 + thickness/2);
      ctx.call<void>("lineTo", width * 0.2 + thickness, height * 0.5 + thickness/2);
      ctx.call<void>("lineTo", width * 0.2 + thickness, height * 0.5 + width * 0.15 - thickness);
      ctx.call<void>("lineTo", width * 0.8 - thickness, height * 0.5 + width * 0.15 - thickness);
      ctx.call<void>("lineTo", width * 0.8 - thickness, height * 0.5 + thickness/2);
      ctx.call<void>("lineTo", width * 0.8 - thickness*2.0, height * 0.5 + thickness/2);
      ctx.call<void>("lineTo", width * 0.8 - thickness*2.0, height * 0.5 + width * 0.15 - thickness*2);
      ctx.call<void>("lineTo", width * 0.5 + centralThickness/2.0, height * 0.5 + width * 0.15 - thickness*2);
      ctx.call<void>("lineTo", width * 0.5 + centralThickness/2.0, height * 0.5 - thickness/2);
      ctx.call<void>("closePath");
      ctx.call<void>("stroke");
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", width * 0.2, height * 0.5);
      // NOTE: this assumes a frame rate of 60 fps. Could change, but later. TODO
      double current = audio::get_example_current();
      ctx.call<void>("translate", emscripten::val(0), emscripten::val(centralThickness/4.0*current));
      ctx.call<void>("lineTo", width * 0.5 - centralThickness/2.0, height * 0.5);
      ctx.call<void>("moveTo", width * 0.5 + centralThickness/2.0, height * 0.5);
      ctx.call<void>("lineTo", width * 0.5 + solenoidThickness/2.0, height * 0.5);
      double tempY = height * 0.5 + solenoidSpacing;
      while (tempY < height * 0.5 + width * 0.15 - thickness*2.5)
      {
        ctx.call<void>("lineTo", width * 0.5 - solenoidThickness/2.0, tempY - solenoidSpacing/2.0);
        ctx.call<void>("lineTo", width * 0.5 - centralThickness/2.0, tempY + centralThickness/(2.0*solenoidThickness)*(solenoidSpacing/2.0) - solenoidSpacing/2.0);
        ctx.call<void>("moveTo", width * 0.5 + centralThickness/2.0, tempY - centralThickness/(2.0*solenoidThickness)*(solenoidSpacing/2.0));
        ctx.call<void>("lineTo", width * 0.5 + solenoidThickness/2.0, tempY);
        tempY += solenoidSpacing;
      }
      ctx.call<void>("translate", emscripten::val(0), emscripten::val(-centralThickness/4.0*current));
      ctx.call<void>("lineTo", width * 0.8 - thickness*2.5, height * 0.5);
      ctx.call<void>("lineTo", width * 0.8, height * 0.5);
      ctx.call<void>("stroke");
      ctx.call<void>("beginPath");
      ctx.set("lineWidth", emscripten::val(2));
      ctx.call<void>("moveTo", width * 0.95, height * 0.5 - width * 0.3);
      ctx.call<void>("translate", emscripten::val(0), emscripten::val(centralThickness/4.0*current));
      ctx.call<void>("lineTo", width * 0.5 + solenoidThickness/2.0, height * 0.5);
      ctx.call<void>("lineTo", width * 0.5 - solenoidThickness/2.0, height * 0.5);
      ctx.call<void>("translate", emscripten::val(0), emscripten::val(-centralThickness/4.0*current));
      ctx.call<void>("lineTo", width * 0.05, height * 0.5 - width * 0.3);
      ctx.call<void>("stroke");
      ctx.call<void>("beginPath");
      ctx.set("lineWidth", emscripten::val(1));
      ctx.call<void>("fillText", emscripten::val("S"), width * 0.5, height * 0.5 + width * 0.15 - thickness*1.5);
      ctx.call<void>("fillText", emscripten::val("N"), width * 0.2 + thickness*1.5, height * 0.5 + width * 0.15 - thickness*1.5);
      ctx.call<void>("fillText", emscripten::val("N"), width * 0.8 - thickness*1.5, height * 0.5 + width * 0.15 - thickness*1.5);
      DrawCurrent(ctx, width*0.1, height*0.5, 10, width * 0.1 * current, "(SLOWED 1000x)", false);
      break;
    }
    case 6:
      DrawFullCircuit(ctx, false, false, false, true);
      break;
    case 7: {
      DrawCurrent(ctx, width * 0.5, height * 0.2, 10, width * 0.1 * audio::get_slowed_current(), "(SLOWED 100x)", false);
      DrawCurrent(ctx, width * 0.7, height * 0.2, 10, width * 0.1 * audio::get_current(), "(REAL TIME)", false);
      DrawFullCircuit(ctx, false, false, true, false);
      break;
    }
    case 8: {
      DrawFullCircuit(ctx, true, false, false, false);
      break;
    }
    case 9:
      DrawTwoCircuits(ctx, false, false, false, false);
      break;
    case 10:
      DrawFourierCircuit(ctx, false, false, false, false);
      break;
    case 11:
    {
      std::string keys[13] = {"Z", "X", "C", "V", "B", "N", "M", ",", "S", "D", "G", "H", "J"};
      for(int i = 0; i < 8; i++) {
        ctx.call<void>("beginPath");
        ctx.call<void>("rect", width*(0.1 + 0.1*i), height*0.25, width*(0.1), height*0.5);
        ctx.call<void>("stroke");
        ctx.call<void>("fillText", keys[i], width*(0.15+0.1*i), height*0.7);
      }
      for(int i = 0; i < 2; i++) {
        ctx.set("fillStyle", emscripten::val("black"));
        ctx.call<void>("beginPath");
        ctx.call<void>("rect", width*(0.17 + 0.1*i), height*0.25, width*(0.06), height*0.3);
        ctx.call<void>("fill");
        ctx.set("fillStyle", emscripten::val("white"));
        ctx.call<void>("fillText", keys[i+8], width*(0.2+0.1*i), height*0.5);
      }
      for(int i = 0; i < 3; i++) {
        ctx.set("fillStyle", emscripten::val("black"));
        ctx.call<void>("beginPath");
        ctx.call<void>("rect", width*(0.47 + 0.1*i), height*0.25, width*(0.06), height*0.3);
        ctx.call<void>("fill");
        ctx.set("fillStyle", emscripten::val("white"));
        ctx.call<void>("fillText", keys[i+10], width*(0.5+0.1*i), height*0.5);
      }
      break;
    }
    default:
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 200 + 100*sin(FRAME_COUNT/(12*pi)), 150 + 75*sin(FRAME_COUNT/(7.5*pi)), abs(50*sin(FRAME_COUNT/(18*pi))), 0, 2 * pi);
      ctx.call<void>("stroke");
  }
  if (!audio::initialized) {
    page = tmp;
  }
  FRAME_COUNT++;
}

void addPlayButton(emscripten::val sidebar)
{
  emscripten::val playButton = document.call<emscripten::val>("createElement", emscripten::val("button"));
  playButton.set("className", emscripten::val("button"));
  playButton.set("id", emscripten::val("play"));
  playButton.set("innerHTML", emscripten::val("PLAY"));
  sidebar.call<void>("appendChild", playButton);
  document.call<emscripten::val>("getElementById", emscripten::val("play")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("PlayOrPauseSound"));
}

void addNextButton(emscripten::val sidebar)
{
  emscripten::val nextButton = document.call<emscripten::val>("createElement", emscripten::val("button"));
  nextButton.set("className", emscripten::val("button"));
  nextButton.set("id", emscripten::val("next"));
  nextButton.set("innerHTML", emscripten::val("NEXT"));
  sidebar.call<void>("appendChild", nextButton);
  document.call<emscripten::val>("getElementById", emscripten::val("next")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("NextPage"));
  document.call<emscripten::val>("getElementById", emscripten::val("next")).set("disabled", emscripten::val(true));
}

void enablePlayButton()
{
  document.call<emscripten::val>("getElementById", emscripten::val("play")).set("disabled", emscripten::val(false));
}

void enablePlayButton(bool off)
{
  document.call<emscripten::val>("getElementById", emscripten::val("play")).set("disabled", emscripten::val(false));
  if(off) {
    document.call<emscripten::val>("getElementById", emscripten::val("play")).set("title", emscripten::val(""));
  }
}

void disablePlayButton(std::string title)
{
  document.call<emscripten::val>("getElementById", emscripten::val("play")).set("disabled", emscripten::val(true));
  document.call<emscripten::val>("getElementById", emscripten::val("play")).set("title", emscripten::val(title));
}

void disablePlayButton()
{
  document.call<emscripten::val>("getElementById", emscripten::val("play")).set("disabled", emscripten::val(true));
}

void enableNextButton()
{
  document.call<emscripten::val>("getElementById", emscripten::val("next")).set("disabled", emscripten::val(false));
}

void disableNextButton()
{
  document.call<emscripten::val>("getElementById", emscripten::val("next")).set("disabled", emscripten::val(true));
}

emscripten::val addInputField(const char* id, bool disabled, double step, double min, double max, double value) {
  emscripten::val field = document.call<emscripten::val>("createElement", emscripten::val("input"));
  field.set("id", emscripten::val(id));
  field.set("type", emscripten::val("number"));
  field.set("disabled", emscripten::val(disabled));
  field.set("step", emscripten::val(step));
  field.set("min", emscripten::val(min));
  field.set("max", emscripten::val(max));
  field.set("value", emscripten::val(value));
  return field;
}

emscripten::val addInputField(const char* id, bool disabled, double step, double min)
{
  emscripten::val field = document.call<emscripten::val>("createElement", emscripten::val("input"));
  field.set("id", emscripten::val(id));
  field.set("type", emscripten::val("number"));
  field.set("disabled", emscripten::val(disabled));
  field.set("step", emscripten::val(step));
  field.set("min", emscripten::val(min));
  return field;
}

emscripten::val addInputField(std::string id, bool disabled, double step)
{
  emscripten::val field = document.call<emscripten::val>("createElement", emscripten::val("input"));
  field.set("id", emscripten::val(id));
  field.set("type", emscripten::val("number"));
  field.set("disabled", emscripten::val(disabled));
  field.set("step", emscripten::val(step));
  return field;
}

void addBreak(emscripten::val sidebar) {
  sidebar.call<void>("appendChild", document.call<emscripten::val>("createElement", emscripten::val("br")));
}

void addParagraph(emscripten::val sidebar, std::string s) {
  emscripten::val p = document.call<emscripten::val>("createElement", emscripten::val("p"));
  p.set("innerHTML", s);
  sidebar.call<void>("appendChild", p);
}

void addParagraph(emscripten::val sidebar, std::string s, std::string id) {
  emscripten::val p = document.call<emscripten::val>("createElement", emscripten::val("p"));
  p.set("innerHTML", s);
  p.set("id", id);
  sidebar.call<void>("appendChild", p);
}

void addBigParagraph(emscripten::val sidebar, std::string s) {
  emscripten::val p = document.call<emscripten::val>("createElement", emscripten::val("p"));
  p.set("innerHTML", s);
  p["classList"].call<void>("add", emscripten::val("big"));
  sidebar.call<void>("appendChild", p);
}

void addLabel(emscripten::val sidebar, std::string f, std::string s) {
  emscripten::val l = document.call<emscripten::val>("createElement", emscripten::val("label"));
  l.set("htmlFor", f);
  l.set("innerHTML", s);
  sidebar.call<void>("appendChild", l);
}

void addLabel(emscripten::val sidebar, std::string f, std::string s, std::string c) {
  emscripten::val l = document.call<emscripten::val>("createElement", emscripten::val("label"));
  l.set("htmlFor", f);
  l.set("className", c);
  l.set("innerHTML", s);
  sidebar.call<void>("appendChild", l);
}

void addCapacitorLabelSet(emscripten::val info, emscripten::val cValue, std::string c, std::string number) {
  addLabel(info, c, "C<sub>" + number +"</sub> = ", "left-label");
  info.call<emscripten::val>("appendChild", cValue);
  addLabel(info, c, "nF");
}

void addSelectOctave(emscripten::val info, std::string id) {
  emscripten::val sel = document.call<emscripten::val>("createElement", emscripten::val("select"));
  sel.set("id", id);
  sel.set("name", id);
  info.call<void>("appendChild", sel);
  emscripten::val c = document.call<emscripten::val>("createElement", emscripten::val("option"));
  c.set("value", "C4");
  c.set("innerHTML", "C4");
  sel.call<void>("appendChild", c);
  emscripten::val cs = document.call<emscripten::val>("createElement", emscripten::val("option"));
  cs.set("value", "C#");
  cs.set("innerHTML", "C#");
  sel.call<void>("appendChild", cs);
  emscripten::val d = document.call<emscripten::val>("createElement", emscripten::val("option"));
  d.set("value", "D");
  d.set("innerHTML", "D");
  sel.call<void>("appendChild", d);
  emscripten::val ds = document.call<emscripten::val>("createElement", emscripten::val("option"));
  ds.set("value", "D#");
  ds.set("innerHTML", "D#");
  sel.call<void>("appendChild", ds);
  emscripten::val e = document.call<emscripten::val>("createElement", emscripten::val("option"));
  e.set("value", "E");
  e.set("innerHTML", "E");
  sel.call<void>("appendChild", e);
  emscripten::val f = document.call<emscripten::val>("createElement", emscripten::val("option"));
  f.set("value", "F");
  f.set("innerHTML", "F");
  sel.call<void>("appendChild", f);
  emscripten::val fs = document.call<emscripten::val>("createElement", emscripten::val("option"));
  fs.set("value", "F#");
  fs.set("innerHTML", "F#");
  sel.call<void>("appendChild", fs);
  emscripten::val g = document.call<emscripten::val>("createElement", emscripten::val("option"));
  g.set("value", "G");
  g.set("innerHTML", "G");
  sel.call<void>("appendChild", g);
  emscripten::val gs = document.call<emscripten::val>("createElement", emscripten::val("option"));
  gs.set("value", "G#");
  gs.set("innerHTML", "G#");
  sel.call<void>("appendChild", gs);
  emscripten::val a = document.call<emscripten::val>("createElement", emscripten::val("option"));
  a.set("value", "A");
  a.set("innerHTML", "A");
  sel.call<void>("appendChild", a);
  emscripten::val as = document.call<emscripten::val>("createElement", emscripten::val("option"));
  as.set("value", "A#");
  as.set("innerHTML", "A#");
  sel.call<void>("appendChild", as);
  emscripten::val b = document.call<emscripten::val>("createElement", emscripten::val("option"));
  b.set("value", "B");
  b.set("innerHTML", "B");
  sel.call<void>("appendChild", b);
  emscripten::val c2 = document.call<emscripten::val>("createElement", emscripten::val("option"));
  c2.set("value", "C5");
  c2.set("innerHTML", "C5");
  sel.call<void>("appendChild", c2);
}

void InitializePage(int i)
{
  emscripten::val info = document.call<emscripten::val>("getElementById", emscripten::val("info"));
  info.set("innerHTML", "");
  switch(i) {
    case (0):
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      enableNextButton();
      addBigParagraph(info, "Welcome to AnaSynth!");
      addParagraph(info, "Shown to the left is the classic LC circuit composed of an inductor (L) and a capacitor (C), oscillating 440 times per second (slowed down 1000x for visualization). These oscillations will go on forever, as long as there is 0 resistance in the circuit.");
      addParagraph(info, "But what if we add a resistor (R) to the circuit, turning the LC circuit into an RLC circuit? The current in RC and RL circuits decrease exponentially as time goes on. But would an RLC circuit also decrease exponentially?");
      break;
    case (1):
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      enableNextButton();
      addParagraph(info, "It turns out that the addition of a resistor does indeed decrease the maximum current exponentially, and the current still oscillates!");
      addParagraph(info, "As a reminder, the time constant of a circuit is the amount of time for the maximum current to decrease by a factor of e.");
      addParagraph(info,
                   "After one time constant, the maximum current will be e^-1 of its original maximum current, or about 36.79% of its original volume.");
      addParagraph(info,
                   "After two time constants, the maximum current will be e^-2 of its original maximum current, or about 13.53% of its original volume.");
      addParagraph(info, "Three time constants will have a maximum current of 4.98%, four will have 1.83%, and so on.");
      addParagraph(info, "The model to the left shows what happens if we repeatedly charge and discharge the capacitator.");
      break;
    case (2): {
      emscripten::val lValue = addInputField("lValue", false, 0.1, 0);
      emscripten::val rValue = addInputField("rValue", true, 0.1, 0);
      emscripten::val tValue = addInputField("tValue", true, 0.1, 0);
      if (inductance != -1)
      {
        lValue.set("value", emscripten::val(inductance));
      }
      if (resistance != -1)
      {
        rValue.set("value", emscripten::val(resistance));
      }
      if (timeConstant != -1)
      {
        tValue.set("value", emscripten::val(timeConstant));
      }

      addParagraph(info, "The time constant, &#120591, of this system is given by the equation");
      addBigParagraph(info, "&#120591 = 2L/R");
      addParagraph(info, "The volume (or more specifically, the sound power, which is then put into a logarithmic scale to give the more familiar sound pressure level measured in decibels) of the sound is proportional to the power through the speaker");
      addParagraph(info,
                   "After one time constant, the volume of the sound will be e^-1 of its original volume, or about 36.79% of its original volume.");
      addParagraph(info,
                   "After two time constants, the volume of the sound will be e^-2 of its original volume, or about 13.53% of its original volume.");
      addParagraph(info, "Three time constants will have a volume of 4.98%, four will have 1.83%, and so on.");
      addParagraph(info,
                   "We want to hear the sound that we make! So let's give the sound a reasonable time constant of at least 1 second.");
      addParagraph(info, "The resistance of the speaker is already given to us.");
      addLabel(info, "lValue", "L = ", "left-label");
      info.call<emscripten::val>("appendChild", lValue);
      addLabel(info, "lValue", "H");
      addBreak(info);
      addBreak(info);
      addLabel(info, "rValue", "R = ", "left-label");
      info.call<emscripten::val>("appendChild", rValue);
      addLabel(info, "rValue", "&#8486");
      addBreak(info);
      addBreak(info);
      addLabel(info, "tValue", "??? &#120591 = ", "left-label");
      info.call<emscripten::val>("appendChild", tValue);
      addLabel(info, "tValue", "s");
      addBigParagraph(info, "GOAL: &#120591 > 1 s");
      addParagraph(info, "RECOMMENDED: &#120591 > 30 s");
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      disableNextButton();
      nextButtonEnabled = false;
      break;
    }
    case (3):
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      enableNextButton();
      addParagraph(info, "Would the frequency of an RLC circuit's oscillations be affected by the resistor? Intuitively, it might make sense to think that the resistor would have \"intertia\" and would slow down the oscillations of the RLC circuit. However, that is surprisingly not the case! Remember: the derivation of the oscillations of an LC circuit looks at the derivative of voltage with respect to time. The voltage across the resistor vanishes.");
      break;
    case (4):
    {
      emscripten::val cValue = addInputField("cValue", false, 0.1, 0);
      emscripten::val lValue = addInputField("lValue", true, 0.1, 0);
      emscripten::val fValue = addInputField("fValue", true, 0.1, 0);
      if (capacitance != -1)
      {
        cValue.set("value", emscripten::val(capacitance));
      }
      if (inductance != -1)
      {
        lValue.set("value", emscripten::val(inductance));
      }
      if (frequency != -1)
      {
        fValue.set("value", emscripten::val(frequency));
      }
      addParagraph(info, "The frequency of an RLC circuit is equal to");
      addBigParagraph(info, "1/( 2?????(LC) )");
      addParagraph(info, "The limits of human hearing is 20 to 20000 Hz, but depending on your age and the quality of your speakers, you might not be able to hear it if you put it at a high or low pitch!");
      addLabel(info, "cValue", "C = ", "left-label");
      info.call<emscripten::val>("appendChild", cValue);
      addLabel(info, "cValue", "nF");
      addBreak(info);
      addBreak(info);
      addLabel(info, "lValue", "L = ", "left-label");
      info.call<emscripten::val>("appendChild", lValue);
      addLabel(info, "lValue", "H");
      addBreak(info);
      addBreak(info);
      addLabel(info, "fValue", "??? f = ", "left-label");
      info.call<emscripten::val>("appendChild", fValue);
      addLabel(info, "fValue", "Hz");
      addBigParagraph(info, "GOAL: 20-20000 Hz");
      addBigParagraph(info, "RECOMMENDED: 200-5000 Hz");
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      disableNextButton();
      frequency = -1;
      break;
    }
    case (5):
      addParagraph(info,
                   "The current that goes through a speaker powers a solenoid (called a \"voice coil\"), which generates magnetic field according to");
      addBigParagraph(info, "B<sub>s</sub> = &#956<sub>0</sub>nI");
      addParagraph(info, "and attracts or repels the magnet that the solenoid is wrapped around.");
      addParagraph(info,
                   "As the solenoid, which is free floating, pulsates away and towards the magnet, it pulls along the speaker cone attached to it, creating alternating waves of low and high air pressure, which we hear as sound");
      addParagraph(info, "To be more specific, by the right hand rule, when the current moves to the right across the speaker, magnetic field points down, so it acts like a magnet with north pointing downwards and attracts to the permanent magnet's south pole and pulls the speaker cone down.");
      addParagraph(info, "Similarly, when the current points to the left, magnetic field points up and pushes the speaker cone up.");
      addParagraph(info, "The conversion of electrical energy to heat and sound is why a speaker has a resistance as energy cannot be created by the first law of thermodynamics.");
      addParagraph(info, "In this example here, the speaker is vibrating at 440 Hz, the \"Concert A\" note. To more clearly see it, time is slowed by a factor of 1000.");
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      enableNextButton();
      break;
    case (6):
    {
      emscripten::val lValue = addInputField("lValue", true, 0.1, 0);
      emscripten::val cValue = addInputField("cValue", true, 0.1, 0);
      emscripten::val vValue = addInputField("vValue", false, 0.1, 0);
      emscripten::val pValue = addInputField("pValue", true, 0.1, 0);
      emscripten::val lpValue = addInputField("lpValue", true, 0.1, 0);
      if (capacitance != -1)
      {
        cValue.set("value", emscripten::val(capacitance));
      }
      if (inductance != -1)
      {
        lValue.set("value", emscripten::val(inductance));
      }
      if (volts != -1)
      {
        vValue.set("value", emscripten::val(volts));
      }
      if (watts != -1)
      {
        pValue.set("value", emscripten::val(watts));
      }
      if (decibels != -1)
      {
        lpValue.set("value", emscripten::val(decibels));
      }

      addParagraph(info, "Adjust your voltage to control the volume. We assume the speaker is 0.5% efficient (audiophiles beware!). dB are being read at 0.55m from the source, because who sits a whole meter away from their speakers? Power is given by");
      addBigParagraph(info, "P = I<sup>2</sup>R = CV<sup>2</sup>/L");
      addParagraph(info, "but power through the speaker is not constant, so we multiply it by half to get average power, which then takes into acccount speaker efficiency to get the dB reading.");
      addLabel(info, "lValue", "L = ", "left-label");
      info.call<emscripten::val>("appendChild", lValue);
      addLabel(info, "lValue", "H");
      addBreak(info);
      addBreak(info);
      addLabel(info, "cValue", "C = ", "left-label");
      info.call<emscripten::val>("appendChild", cValue);
      addLabel(info, "cValue", "nF");
      addBreak(info);
      addBreak(info);
      addLabel(info, "vValue", "v = ", "left-label");
      info.call<emscripten::val>("appendChild", vValue);
      addLabel(info, "vValue", "mV");
      addBreak(info);
      addBreak(info);
      addLabel(info, "pValue", "P = ", "left-label");
      info.call<emscripten::val>("appendChild", pValue);
      addLabel(info, "pValue", "&microW");
      addBreak(info);
      addBreak(info);
      addLabel(info, "lpValue", "??? L<sub>p</sub> = ", "left-label");
      info.call<emscripten::val>("appendChild", lpValue);
      addLabel(info, "lpValue", "dB");
      addBigParagraph(info, "GOAL: 30-70 dB");
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      disableNextButton();
      break;
    }
    case (7): {
      std::string perfectlyEfficient = std::to_string(audio::watts_to_decibels(1, 1));
      std::string extraordinarilyEfficient = std::to_string(audio::decibels_to_watts(105, 1) * 100) + "%";
      std::string veryEfficient = std::to_string(audio::decibels_to_watts(95, 1) * 100) + "%";
      std::string average = std::to_string(audio::decibels_to_watts(88, 1) * 100) + "%";
      std::string veryInefficient = std::to_string(audio::decibels_to_watts(83, 1) * 100) + "%";

      emscripten::val rValue = addInputField("rValue", false, 2, 0, 16, 6);
      emscripten::val sensitivityValue = addInputField("sensitivityValue", false, 0.1, 0, 109.453876, 88);
      emscripten::val efficiencyValue = addInputField("efficiencyValue", true, 0.01);
      rValue.set("value", emscripten::val(resistance));
      sensitivityValue.set("value", emscripten::val(efficiency));

      addParagraph(info, "Now, we give you the freedom to manipulate the statistics of the speaker.");
      addParagraph(info, "The resistance of the speaker, which would be called the speaker impedance in an AC circuit, controls the time constant and the initial sound volume. Since it changes two key values rather than just one, we have restricted changing its value until now.");
      addParagraph(info, "Speaker sensitivity, which is a measure of the efficiency of a speaker to convert electrical to sound energy, is often measured as the volume of the sound from a distance of 1 meter when 1 watt is passed through it.");
      addParagraph(info, "It will also affect the initial sound volume, though it does not change the actual resistance of the speaker and so does not affect the time constant");
      addParagraph(info, "A perfectly efficient speaker has a sensitivity of " + perfectlyEfficient + "dB. Real world speakers have quite a range of efficiencies:");
      addParagraph(info, "Extraordinarily efficient speaker: 105 dB (" + extraordinarilyEfficient + " efficiency)");
      addParagraph(info, "Very efficient speaker: 95 dB (" + veryEfficient + " efficiency)");
      addParagraph(info, "Average efficiency speaker: 88 dB (" + average + " efficiency)");
      addParagraph(info, "Very inefficient speaker: 83 dB (" + veryInefficient + " efficiency)");
      addParagraph(info,
                   "Note that manipulating the values of the speaker may cause values entered in previously to become invalid.");
      addLabel(info, "rValue", "R = ", "left-label");
      info.call<emscripten::val>("appendChild", rValue);
      addLabel(info, "rValue", "&#8486");
      addBreak(info);
      addBreak(info);
      addLabel(info, "sensitivityValue", "dB<sub>1m</sub> = ", "left-label");
      info.call<emscripten::val>("appendChild", sensitivityValue);
      addLabel(info, "sensitivityValue", "dB");
      addBreak(info);
      addBreak(info);
      addLabel(info, "efficiencyValue", "??? ?? = ", "left-label");
      info.call<emscripten::val>("appendChild", efficiencyValue);
      addLabel(info, "efficiencyValue", "% efficiency");
      enablePlayButton();
      enableNextButton();
      break;
    }
    case(8):
    {
      emscripten::val c1Value = addInputField("c1Value", false, 0.1, 0);
      emscripten::val c2Value = addInputField("c2Value", false, 0.1, 0);
      emscripten::val c3Value = addInputField("c3Value", false, 0.1, 0);
      emscripten::val c4Value = addInputField("c4Value", false, 0.1, 0);
      emscripten::val c5Value = addInputField("c5Value", false, 0.1, 0);
      emscripten::val c6Value = addInputField("c6Value", false, 0.1, 0);
      emscripten::val c7Value = addInputField("c7Value", false, 0.1, 0);
      emscripten::val c8Value = addInputField("c8Value", false, 0.1, 0);
      emscripten::val c9Value = addInputField("c9Value", false, 0.1, 0);
      emscripten::val c10Value = addInputField("c10Value", false, 0.1, 0);
      emscripten::val c11Value = addInputField("c11Value", false, 0.1, 0);
      emscripten::val c12Value = addInputField("c12Value", false, 0.1, 0);
      emscripten::val c13Value = addInputField("c13Value", false, 0.1, 0);
      emscripten::val fValue = addInputField("fValue", true, 0.1, 0);

      addParagraph(info, "Time to create a scale! Match each frequency as closely as you can.");
      addParagraph(info, "Match: C", "match");
      addLabel(info, "c1Value", "C<sub>4</sub>: 261.63 Hz");
      addCapacitorLabelSet(info, c1Value, "c1Value", "1");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c2Value", "C#<sub>4</sub>: 277.18 Hz");
      addCapacitorLabelSet(info, c2Value, "c2Value", "2");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c3Value", "D<sub>4</sub>: 293.66 Hz");
      addCapacitorLabelSet(info, c3Value, "c3Value", "3");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c4Value", "D#<sub>4</sub>: 311.13 Hz");
      addCapacitorLabelSet(info, c4Value, "c4Value", "4");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c5Value", "E<sub>4</sub>: 329.63 Hz");
      addCapacitorLabelSet(info, c5Value, "c5Value", "5");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c6Value", "F<sub>4</sub>: 349.23 Hz");
      addCapacitorLabelSet(info, c6Value, "c6Value", "6");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c7Value", "F#<sub>4</sub>: 369.99 Hz");
      addCapacitorLabelSet(info, c7Value, "c7Value", "7");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c8Value", "G<sub>4</sub>: 392.00 Hz");
      addCapacitorLabelSet(info, c8Value, "c8Value", "8");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c9Value", "G#<sub>4</sub>: 415.30 Hz");
      addCapacitorLabelSet(info, c9Value, "c9Value", "9");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c10Value", "A<sub>4</sub>: 440.00 Hz");
      addCapacitorLabelSet(info, c10Value, "c10Value", "10");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c11Value", "A#<sub>4</sub>: 466.16 Hz");
      addCapacitorLabelSet(info, c11Value, "c11Value", "11");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c12Value", "B<sub>4</sub>: 493.88 Hz");
      addCapacitorLabelSet(info, c12Value, "c12Value", "12");
      addBreak(info);
      addBreak(info);
      addLabel(info, "c13Value", "C<sub>5</sub>: 523.25 Hz");
      addCapacitorLabelSet(info, c13Value, "c13Value", "13");
      addBreak(info);
      addBreak(info);
      addLabel(info, "fValue", "??? f = ", "left-label");
      info.call<emscripten::val>("appendChild", fValue);
      addLabel(info, "fValue", "Hz");
      disablePlayButton();
      playButtonEnabled = false;
      frequency = -1;
      enableNextButton();
      break;
    }
    case (9):
      addParagraph(info, "And what if we put multiple LC components together?");
      addParagraph(info, "The waves now add up!");
      addParagraph(info, "So how about some harmonics?");
      addParagraph(info, "Quick tip: C + E, C + G, and C + C are harmonious, while C + C# and C + F# are dissonant.");
      addParagraph(info, "Have fun messing around!");
      addBreak(info);
      addLabel(info, "s1", "Note 1:", "note-label");
      addSelectOctave(info, "s1");
      addBreak(info);
      addBreak(info);
      addLabel(info, "s2", "Note 2:", "note-label");
      addSelectOctave(info, "s2");
      enablePlayButton();
      enableNextButton();
      break;
    case (10):
    {
      addParagraph(info, "With harmonics, we can use Fourier transforms to create more complex waveforms. For example, we can combine 10 RLC circuits to create a 10th order approximation of a sawtooth wave. Try changing the sawtooth wave's frequency!");
      emscripten::val fValue = addInputField("fValue", false, 0.1, 0);

      addLabel(info, "fValue", "f = ", "left-label");
      info.call<emscripten::val>("appendChild", fValue);
      addLabel(info, "fValue", "Hz");
      enablePlayButton();
      enableNextButton();
      break;
    }
    case(11) :
    {
      addParagraph(info, "Using the notes you have created and the sawtooth wave that you have generated, you can now play your very own tunes on this piano!");
      emscripten::val sel = document.call<emscripten::val>("createElement", emscripten::val("select"));
      sel.set("id", "wave");
      sel.set("name", "wave");
      info.call<void>("appendChild", sel);
      emscripten::val sine = document.call<emscripten::val>("createElement", emscripten::val("option"));
      sine.set("value", "sine");
      sine.set("innerHTML", "Sine");
      sel.call<void>("appendChild", sine);
      emscripten::val saw = document.call<emscripten::val>("createElement", emscripten::val("option"));
      saw.set("value", "saw");
      saw.set("innerHTML", "Sawtooth");
      sel.call<void>("appendChild", saw);

      for(int i = 0; i < 13; i++) {
        pianoKeys.emplace_back(false);
        previousKeys.emplace_back(false);
      }
      audio::remove_all_rlcs();
      std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> defaults;
      for (int i = 0; i < 13; i++) {
        std::vector<boost::uuids::uuid> s1;
        for (int j = 1; j < 11; j++) {
          boost::uuids::uuid uuid = uuidGenerator();
          defaults.try_emplace(uuid, std::make_tuple(frequencyArray[i] * j, initialVolume / double(j), timeConstant));
          s1.emplace_back(uuid);
        }
        pianouuids.emplace_back(s1);
      }
      audio::add_rlcs(defaults);
      enablePlayButton();
      disableNextButton();
      break;
    }
    default:
      printf("page out of range\n");
      break;
  }
}

void StoreData(int page); // forward declaration

void RenderSidebar()
{
  switch(page) {
    case (0):
      break;
    case (2): {
      emscripten::val inductor = document.call<emscripten::val>("getElementById", emscripten::val("lValue"));
      emscripten::val resistor = document.call<emscripten::val>("getElementById", emscripten::val("rValue"));
      emscripten::val tConstant = document.call<emscripten::val>("getElementById", emscripten::val("tValue"));
      double tV = 0;
      resistor.set("value", emscripten::val(resistance));
      if (inductor["value"].as<std::string>() != "" && resistor["value"].as<std::string>() != "") {
        tV = 2 * stod(inductor["value"].as<std::string>()) / stod(resistor["value"].as<std::string>());
        if (std::isinf(tV)) {
          tConstant.set("value", emscripten::val("Infinity"));
        } else {
          tConstant.set("value", emscripten::val(tV));
        }
      }

      emscripten::val sidebar = document.call<emscripten::val>("getElementById", emscripten::val("sidebar"));
      emscripten::val next = document.call<emscripten::val>("getElementById", emscripten::val("next"));
      if (tV > 1) {
        if (!nextButtonEnabled)
        {
          enableNextButton();
          nextButtonEnabled = true;
        }
        if (timeConstant != tV) {
          if (audio::get_playing()) {
            PlayOrPauseSound(emscripten::val(""));
          }
          boost::uuids::uuid uuid;
          uuid = uuidGenerator();
          std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> defaults = {{uuid, std::make_tuple(frequency, initialVolume, timeConstant)}};
          audio::add_rlcs(defaults);
          inductance = stod(inductor["value"].as<std::string>());
          timeConstant = tV;
          StoreData(page);
        }
      } else if (tV < 1 && nextButtonEnabled) {
        disableNextButton();
        nextButtonEnabled = false;
      }
      break;
    }
    case 4:
    {
      emscripten::val capacitor = document.call<emscripten::val>("getElementById", emscripten::val("cValue"));
      emscripten::val fr = document.call<emscripten::val>("getElementById", emscripten::val("fValue"));
      double f;
      if (capacitor["value"].as<std::string>() != "") {
        f = 1 / (2 * pi * sqrt(stod(capacitor["value"].as<std::string>()) / 1000000000 * inductance));
        if (std::isinf(f)) {
          fr.set("value", emscripten::val("Infinity"));
        } else {
          fr.set("value", emscripten::val(f));
          if (f < 20000 && f > 20) {
            if (frequency != f) {
              enableNextButton();
              capacitance = stod(capacitor["value"].as<std::string>());
              frequency = f;
              if (audio::get_playing()) {
                PlayOrPauseSound(emscripten::val(""));
              }
              audio::remove_all_rlcs();
              std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> defaults{
                      {uuidGenerator(), std::make_tuple(frequency, initialVolume, timeConstant)}};
              audio::add_rlcs(defaults);
              StoreData(page);
            }
          }
        }
      }
      break;
    }
    case 6:
    {
      emscripten::val voltage = document.call<emscripten::val>("getElementById", emscripten::val("vValue"));
      emscripten::val power = document.call<emscripten::val>("getElementById", emscripten::val("pValue"));
      emscripten::val volume = document.call<emscripten::val>("getElementById", emscripten::val("lpValue"));
      double p;
      if (voltage["value"].as<std::string>() != "") {
        p = 0.5* capacitance * stod(voltage["value"].as<std::string>()) * stod(voltage["value"].as<std::string>()) / inductance;
        if (std::isinf(p)) {
          power.set("value", emscripten::val("Infinity"));
          volume.set("value", emscripten::val("Infinity"));
        } else {
          power.set("value", emscripten::val(p));
          volume.set("value", emscripten::val(
                  audio::watts_to_decibels(audio::decibels_to_watts(efficiency, 1) * p / 1000000, 0.55)));
          if (watts != p && audio::watts_to_decibels(audio::decibels_to_watts(efficiency, 1) * p / 1000000, 0.55) > 30 && audio::watts_to_decibels(audio::decibels_to_watts(efficiency, 1) * p / 1000000, 0.55) < 70) {
            if (audio::get_playing()) {
              PlayOrPauseSound(emscripten::val(""));
            }
            watts = p;
            initialVolume = p * audio::decibels_to_watts(efficiency, 1);
            volts = stod(voltage["value"].as<std::string>());
            audio::remove_all_rlcs();
            std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> defaults{{uuidGenerator(), std::make_tuple(frequency, initialVolume, timeConstant)}};
            audio::add_rlcs(defaults);
            StoreData(page);
            enableNextButton();
          }
        }
      }
      // TODO: initialVolume adjustment
      break;
    }
    case 7:
    {
      emscripten::val rValue = document.call<emscripten::val>("getElementById", emscripten::val("rValue"));
      emscripten::val sensitivity = document.call<emscripten::val>("getElementById", emscripten::val("sensitivityValue"));
      emscripten::val efficiencyVal = document.call<emscripten::val>("getElementById", emscripten::val("efficiencyValue"));
      double efficiencyValue = audio::decibels_to_watts(stod(sensitivity["value"].as<std::string>()), 1);
      efficiencyVal.set("value", emscripten::val(efficiencyValue*100));
      if (rValue["value"].as<std::string>() != "" && sensitivity["value"].as<std::string>() != "") {
        double efficiencyValue = audio::decibels_to_watts(stod(sensitivity["value"].as<std::string>()), 1);
        efficiencyVal.set("value", emscripten::val(efficiencyValue*100));
        double r = stod(rValue["value"].as<std::string>());
        double t = 2 * inductance / r;
        std::vector<double> f = {frequency};
        resistance = r;
        
        // std::cout << std::to_string(watts/4 * r) << std::endl;
        if(playButtonEnabled && audio::decibels_to_watts(efficiency, 1) * watts / 1000000 > 1) {
          disablePlayButton("Please lower the volume.");
          playButtonEnabled = false;
        } else if (!playButtonEnabled && audio::decibels_to_watts(efficiency, 1) * watts / 1000000 <= 1){
          enablePlayButton(true);
          playButtonEnabled = true;
        }

        static std::vector<double> previousVars;
        std::vector<double> vars = {watts, r, t, stod(efficiencyVal["value"].as<std::string>())};
        if(previousVars != vars) {
          //audio::set_vars(f, watts/4 * r, t);
          // TODO: set vars
          efficiency = stod(sensitivity["value"].as<std::string>());
          timeConstant = t;
          initialVolume = watts * audio::decibels_to_watts(efficiency, 1);
          audio::remove_all_rlcs();
          std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> defaults{{uuidGenerator(), std::make_tuple(frequency, initialVolume, timeConstant)}};
          audio::add_rlcs(defaults);
          previousVars = vars;
          StoreData(page);
        }
      }
      circuitCompleted = true;
      break;
    }
    case 8:
    {
      static int counter = 1;
      double cv;
      emscripten::val match = document.call<emscripten::val>("getElementById", emscripten::val("match"));
      emscripten::val fValue = document.call<emscripten::val>("getElementById", emscripten::val("fValue"));
      switch(counter) {
        case(1):
          match.set("innerHTML", "Match: C4");
          break;
        case(2):
          match.set("innerHTML", "Match: C#");
          break;
        case(3):
          match.set("innerHTML", "Match: D");
          break;
        case(4):
          match.set("innerHTML", "Match: D#");
          break;
        case(5):
          match.set("innerHTML", "Match: E");
          break;
        case(6):
          match.set("innerHTML", "Match: F");
          break;
        case(7):
          match.set("innerHTML", "Match: F#");
          break;
        case(8):
          match.set("innerHTML", "Match: G");
          break;
        case(9):
          match.set("innerHTML", "Match: G#");
          break;
        case(10):
          match.set("innerHTML", "Match: A");
          break;
        case(11):
          match.set("innerHTML", "Match: A#");
          break;
        case(12):
          match.set("innerHTML", "Match: B");
          break;
        case(13):
          match.set("innerHTML", "Match: C5");
          break;
        default:
          break;
      }
      if (document.call<emscripten::val>("getElementById", "c" + std::to_string(counter) + "Value")["value"].as<std::string>() != "") {
        cv = stod(document.call<emscripten::val>("getElementById", "c" + std::to_string(counter) + "Value")["value"].as<std::string>());
        double f = 1 / (2 * pi * sqrt(cv / 1000000000 * inductance));
        fValue.set("value", f);
        static std::vector<double> previousVars;
        std::vector<double>vars = {f, watts, resistance, inductance};
        frequency = f;
        if (previousVars != vars)
        {
          if(!playButtonEnabled) {
            enablePlayButton();
            playButtonEnabled = true;
          }
          std::vector<double>freqs = {f};
          //audio::set_vars(freqs, watts/4 * resistance, 2*inductance/resistance);
          if (audio::get_playing()) {
            PlayOrPauseSound(emscripten::val(""));
          }
          audio::remove_all_rlcs();
          std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> defaults;
          for (auto freq : freqs) {
            boost::uuids::uuid uuid = uuidGenerator();
            defaults.try_emplace(uuid, std::make_tuple(freq, initialVolume, timeConstant));
          }
          audio::add_rlcs(defaults);
          previousVars = vars;
          StoreData(page);
        }

      
        if(abs(f - frequencyArray[counter - 1]) < 2) {
          document.call<emscripten::val>("getElementById", "c" + std::to_string(counter) + "Value").set("disabled", true);
          counter++;
        }
      }
      
      circuitCompleted = true;
      break;
    }
    case 9:
    {
      static std::vector<double> previousFreqs;
      std::vector<double>freqs = {frequencyMap.at(document.call<emscripten::val>("getElementById", emscripten::val("s1"))["value"].as<std::string>())};
      freqs.emplace_back(frequencyMap.at(document.call<emscripten::val>("getElementById", emscripten::val("s2"))["value"].as<std::string>()));


      if (previousFreqs != freqs) {
        if (audio::get_playing()) {
          PlayOrPauseSound(emscripten::val(""));
        }
        audio::remove_all_rlcs();
        std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> defaults;
        for (auto freq : freqs) {
          boost::uuids::uuid uuid = uuidGenerator();
          defaults.try_emplace(uuid, std::make_tuple(freq, initialVolume, timeConstant));
        }
        audio::add_rlcs(defaults);
        previousFreqs = freqs;
        StoreData(page);
      }
      break;
    }
    case 10:
    {
      static double previousFr;
      
      if(document.call<emscripten::val>("getElementById", emscripten::val("fValue"))["value"].as<std::string>() != "") {
        double fr = stod(document.call<emscripten::val>("getElementById", emscripten::val("fValue"))["value"].as<std::string>());

        if (previousFr != fr) {
          if (audio::get_playing()) {
            PlayOrPauseSound(emscripten::val(""));
          }
          audio::remove_all_rlcs();
          std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> defaults;
          for (int i = 1; i < 11; i++) {
            boost::uuids::uuid uuid = uuidGenerator();
            defaults.try_emplace(uuid, std::make_tuple(fr * i, initialVolume / double(i), timeConstant));
          }
          audio::add_rlcs(defaults);
          previousFr = fr;
          StoreData(page);
        }
      }
      break;
    }
    case 11:
    {
      std::vector<bool> keys = pianoKeys;
      for(int i = 0; i < 13; i++) {
        if(previousKeys.at(i) != keys.at(i)) {
          // std::cout << i << std::endl;
          std::string waveform = document.call<emscripten::val>("getElementById", emscripten::val("wave"))["value"].as<std::string>();
          std::vector<boost::uuids::uuid> fr;
          if(waveform == "sine") {
            fr = {pianouuids.at(i).at(0)};
          } else if(waveform == "saw") {
            for(int j = 1; j < 11; j++) {
              fr.emplace_back(pianouuids.at(i).at(j-1));
            }
          }
          // if(keys.at(i)) {
          audio::play_or_stop(fr);
          // } else if(!keys.at(i)) {
          //   audio::remove_rlcs(fr);
          // }
        }
      }
      previousKeys = keys;
      break;
    }
    default:
      break;
  }
}

void Render()
{
  RenderCanvas();
  RenderSidebar();
}

extern "C"
{
EMSCRIPTEN_KEEPALIVE
void SelectPage(int i)
{
  page = i;
  InitializePage(page);
  StoreData(page);
  document.call<emscripten::val>("getElementById", emscripten::val("b" + std::to_string(page+1))).call<void>("setAttribute", emscripten::val("checked"), emscripten::val("checked"));
}
void NextPage(emscripten::val event)
{
  SelectPage(++page);
}
}

void StoreData(int page)
{
  emscripten::val localStorage = emscripten::val::global("localStorage");
  localStorage.call<void>("setItem", emscripten::val("selectedPage"), emscripten::val(page));
  std::string temp = "";
  for (auto& [uuid, frequency] : audio::get_frequencies())
  {
    temp += std::to_string(frequency);
    temp += ",";
  }
  if (temp.length() != 0) {
    temp.pop_back();
  }
  localStorage.call<void>("setItem", emscripten::val("frequencies"), emscripten::val(temp));
  temp = "";
  for (auto& [uuid, initialVolume] : audio::get_initial_volumes())
  {
    temp += std::to_string(initialVolume);
    temp += ",";
  }
  if (temp.length() != 0) {
    temp.pop_back();
  }
  localStorage.call<void>("setItem", emscripten::val("initialVolumes"), emscripten::val(temp));
  temp = "";
  for (auto& [uuid, timeConstant] : audio::get_time_constants())
  {
    temp += std::to_string(timeConstant);
    temp += ",";
  }
  if (temp.length() != 0) {
    temp.pop_back();
  }
  localStorage.call<void>("setItem", emscripten::val("timeConstants"), emscripten::val(temp));
}

void RetrieveData()
{
  emscripten::val localStorage = emscripten::val::global("localStorage");
  emscripten::val pageNumber = localStorage.call<emscripten::val>("getItem", emscripten::val("selectedPage"));
  emscripten::val timeConstant = localStorage.call<emscripten::val>("getItem", emscripten::val("timeConstants"));
  emscripten::val initialVolume = localStorage.call<emscripten::val>("getItem", emscripten::val("initialVolumes"));
  emscripten::val frequency = localStorage.call<emscripten::val>("getItem", emscripten::val("frequencies"));
  // checks if there is such a stored value: typeOf will be "object" when the emscripten::val is null

  std::vector<double> frequencies;
  std::vector<double> initialVolumes;
  std::vector<double> timeConstants;
  if (frequency.typeOf().as<std::string>() == "string") {
    std::string frequenciesString = frequency.as<std::string>();
    while (frequenciesString != "")
    {
      int pos = frequenciesString.find(",");
      frequencies.push_back(std::stod(frequenciesString.substr(0, pos)));
      frequenciesString = frequenciesString.substr(pos+1);
      if (pos == std::string::npos) {break;}
    }
  } else {
    frequencies = {261.63, 329.63, 392.00}; // C major chord
  }
  if (initialVolume.typeOf().as<std::string>() == "string") {
    std::string string = initialVolume.as<std::string>();
    while (string != "")
    {
      int pos = string.find(",");
      initialVolumes.push_back(std::stod(string.substr(0, pos)));
      string = string.substr(pos+1);
      if (pos == std::string::npos) {break;}
    }
  } else {
    initialVolumes = {0.3, 0.3, 0.3};
  }
  if (timeConstant.typeOf().as<std::string>() == "string") {
    std::string string = timeConstant.as<std::string>();
    while (string != "")
    {
      int pos = string.find(",");
      timeConstants.push_back(std::stod(string.substr(0, pos)));
      string = string.substr(pos+1);
      if (pos == std::string::npos) {break;}
    }
  } else {
    timeConstants = {1.5, 1.5, 1.5};
  }

  std::unordered_map<boost::uuids::uuid, std::tuple<double, double, double>, boost::hash<boost::uuids::uuid>> insertion;
  for (int i = 0; i < frequencies.size(); i++)
  {
    boost::uuids::uuid uuid = uuidGenerator();
    uuids.emplace_back(uuid);
    insertion.try_emplace(uuid, std::make_tuple(frequencies.at(i), initialVolumes.at(i), timeConstants.at(i)));
  }
  audio::add_rlcs(insertion);

  if (pageNumber.typeOf().as<std::string>() == "string") {
    int page = std::stoi(pageNumber.as<std::string>());
    SelectPage(page);
  } else {
    SelectPage(0);
  }
}

void CloseIntro(emscripten::val event) {
  document.call<emscripten::val>("getElementById", emscripten::val("blur")).call<void>("remove", emscripten::val("mouseup"));
  audio::initialize();
  // RetrieveData();
  StoreData(page);
}

int main() {
  document.call<void>("addEventListener",
                      emscripten::val("mousemove"),
                      emscripten::val::module_property("InteractWithCanvas"));
  document.call<void>("addEventListener",
                      emscripten::val("mousedown"),
                      emscripten::val::module_property("InteractWithCanvas"));
  document.call<void>("addEventListener",
                      emscripten::val("mouseup"),
                      emscripten::val::module_property("InteractWithCanvas"));
                      
  document.call<void>("addEventListener",
                      emscripten::val("keydown"),
                      emscripten::val::module_property("InteractWithKeyboard"));
  document.call<void>("addEventListener",
                      emscripten::val("keyup"),
                      emscripten::val::module_property("InteractWithKeyboard"));


  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  canvas.set("width", emscripten::val(window["innerWidth"].as<double>() * 0.7));
  canvas.set("height", emscripten::val(window["innerHeight"].as<double>() - 80));
  window.call<void>("addEventListener", emscripten::val("resize"), emscripten::val::module_property("ResizeCanvas"));
  ctx.set("textAlign", emscripten::val("center"));
  ctx.set("textBaseline", emscripten::val("middle"));
  ctx.set("font", emscripten::val("20px Calibri"));
  document.call<emscripten::val>("getElementById", emscripten::val("next")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("NextPage"));
  document.call<emscripten::val>("getElementById", emscripten::val("play")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("PlayOrPauseSound"));
  document.call<emscripten::val>("getElementById", emscripten::val("intro-button")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("CloseIntro"));

  // must be the last command in main()
  emscripten_set_main_loop(Render, 0, 1);
  return 0;
}


EMSCRIPTEN_BINDINGS(bindings)
{
  emscripten::function("InteractWithCanvas", InteractWithCanvas);
  emscripten::function("InteractWithKeyboard", InteractWithKeyboard);
  emscripten::function("ResizeCanvas", ResizeCanvas);
  emscripten::function("SelectPage", SelectPage);
  emscripten::function("NextPage", NextPage);
  emscripten::function("VolumeControl", audio::volume_control);
  emscripten::function("PlayOrPauseSound", PlayOrPauseSound);
  emscripten::function("CloseIntro", CloseIntro);
}
