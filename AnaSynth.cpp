#include <emscripten/val.h>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

#include <iostream>
#include <numbers>
#include <cmath>
#include <vector>
#include <optional>
#include <random>
#include <vector>

emscripten::val window = emscripten::val::global("window");
emscripten::val document = emscripten::val::global("document");
const double pi = std::numbers::pi;
const double e = std::numbers::e;
static int page;
bool circuitCompleted = false;

namespace audio
{
  emscripten::val globalAudioContext = emscripten::val::global("AudioContext");
  std::vector<emscripten::val> oscillators;
  // audioContext is allowed to start only after user interactions, so this must only be created when initialized
  std::optional<emscripten::val> audioContext;
  std::optional<emscripten::val> gainNode;
  std::optional<emscripten::val> volumeManager;
  double TIME_CONSTANT = 1.5; // in seconds
  double beginTime = 0.0;
  bool initialized = false;
  bool playing = false;
  double initialVolume = 0.5;
  int timeConstants = 0;
  std::vector<double> _frequencies;
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
  void set_frequencies(std::vector<double>& frequencies)
  {
    if (initialized)
    {
      // This part can be called ONLY after audio::initialize() is called
      // emscripten currently does not have much support for throwing std::exception
      for (auto &oscillator: oscillators)
      {
        oscillator.call<void>("stop");
        oscillator.call<void>("disconnect"); // disconnect oscillator from audio output
      }
      oscillators.clear();
      for (auto &frequency: frequencies) {
        emscripten::val oscillator = audioContext.value().call<emscripten::val>("createOscillator");
        oscillator.set("type", emscripten::val("sine"));
        oscillator["frequency"].set("value", emscripten::val(frequency));
        oscillators.emplace_back(oscillator);
      }
      // start the oscillators together
      for (auto &oscillator: oscillators) {
        oscillator.call<void>("start");
      }
    }
    _frequencies = frequencies;
  }
  void set_initial_volume(double startingVolume)
  {
    initialVolume = startingVolume;
  }
  void set_time_constant(double timeConstant)
  {
    TIME_CONSTANT = timeConstant;
  }
  void set_vars(std::vector<double>& frequencies, double startingVolume, double timeConstant)
  {
    set_frequencies(frequencies);
    set_initial_volume(startingVolume);
    set_time_constant(timeConstant);
  }
  bool get_playing()
  {
    return playing;
  }
  std::vector<double>& get_frequencies()
  {
    return _frequencies;
  }
  double get_initial_volume()
  {
    return initialVolume;
  }
  double get_current_volume()
  {
    if (playing) {
      return initialVolume * pow(e, -((audioContext.value()["currentTime"].as<double>() - beginTime) / TIME_CONSTANT));
    } else {
      return 0;
    }
  }
  double get_current()
  {
    if (playing)
    {
      double temp = 0.0;
      for (auto& frequency : _frequencies)
      {
        temp += sin(2*pi*frequency*(audioContext.value()["currentTime"].as<double>() - beginTime));
      }
      return get_current_volume() * temp;
    } else {
      return 0;
    }
  }
  double get_slowed_current()
  {
    if (playing)
    {
      double temp = 0.0;
      for (auto& frequency : _frequencies)
      {
        temp += sin(2*pi*frequency*(audioContext.value()["currentTime"].as<double>() - beginTime)/100);
      }
      return get_current_volume() * temp;
    } else {
      return 0;
    }
  }
  double get_example_current()
  {
    // always playes a 440 Hz sound at a volume of 1 and slows it down by a factor of 1,000 (performace.now() is in milliseconds)
    return sin(2*pi*440*(emscripten::val::global("performance").call<emscripten::val>("now").as<double>()/1000)/1000);
  }
  double get_time_constant()
  {
    return TIME_CONSTANT;
  }
  void initialize()
  {
    // because you cannot create audioContext until user interaction with the page, a rule enforced by browsers
    if (!initialized)
    {
      emscripten::val baseAudioContext = globalAudioContext.new_();
      audioContext.emplace(baseAudioContext);
      gainNode.emplace(audioContext.value().call<emscripten::val>("createGain"));
      gainNode.value().call<void>("connect", audioContext.value()["destination"]); // connect gainNode to audio output
      initialized = true;
      set_frequencies(_frequencies);
    }
  }
  void volume_control()
  {
    if (audioContext.has_value() && gainNode.has_value())
    {
      // time after play() in seconds. add one TIME_CONSTANT since this is the time at the end of the exponentialRampToValueAtTime
      double offsetTime = audioContext.value()["currentTime"].as<double>() - beginTime;
      if (round(offsetTime/TIME_CONSTANT) == timeConstants)
      {
        timeConstants = round(offsetTime/TIME_CONSTANT) + 1;
        if (pow(e, -timeConstants) > 2*pow(10,-45)) // can't exponentialRampToValueAtTime to 0 if timeConstants is too high
         {
          std::cout << offsetTime << " sec to " << offsetTime+TIME_CONSTANT << " sec: decrease to volume of " << 100*pow(e, -timeConstants) << "% (e^-" << timeConstants << ") from volume " << gainNode.value()["gain"]["value"].as<double>() << " at " << timeConstants << " time constants\n";
          gainNode.value()["gain"].call<void>("setValueAtTime", gainNode.value()["gain"]["value"], audioContext.value()["currentTime"]);
          gainNode.value()["gain"].call<emscripten::val>("exponentialRampToValueAtTime",
                                                         emscripten::val(initialVolume * pow(e, -timeConstants)),
                                                         emscripten::val(beginTime + offsetTime + TIME_CONSTANT));
        }
      }
    }
  }
  void play_or_pause()
  {
    if (!initialized)
    {
      // This function can be called ONLY after audio::initialize() is called
      // emscripten currently does not have much support for throwing std::exception
      std::cout << "Error: audio::play() called before audio::initialize()\n";
    }
    if (playing)
    {
      emscripten::val currentTime = audioContext.value()["currentTime"];
      gainNode.value()["gain"].call<void>("cancelScheduledValues", currentTime);
      // fade out in 0.1 seconds - doesn't actually work because of disconnect so, if time leftover, TODO
      gainNode.value()["gain"].call<emscripten::val>("exponentialRampToValueAtTime",
                                                     emscripten::val(0.000001),
                                                     emscripten::val(currentTime.as<double>() + 0.1));
      for (auto &oscillator: oscillators)
      {
        oscillator.call<void>("disconnect", gainNode.value());
      }
      window.call<void>("clearInterval", volumeManager.value());
      timeConstants = 0;
      playing = false;
    } else {
      // play
      for (auto &oscillator: oscillators)
      {
        oscillator.call<void>("connect", gainNode.value());
      }
      beginTime = audioContext.value()["currentTime"].as<double>();
      // mute sound then go to full volume in 0.05 seconds
      gainNode.value()["gain"].set("value", emscripten::val(initialVolume));
      volume_control();
      volumeManager.emplace(
              window.call<emscripten::val>("setInterval", emscripten::val::module_property("VolumeControl"),
                                           emscripten::val(TIME_CONSTANT * 1000)));
      playing = true;
    }
  }
  double watts_to_decibels(double power, double distance)
  {
    // assumes Normal Temperature and Pressure, aka 20 degrees Celsius,	101.325 kPa
    // here, air density is 1.2923 kg/m^3 and the speed of sound is 343 m/s
    // assumes reference sound pressure (human hearing threshold) is 20 μPa
    // also assumes the listener's eardrums are perfectly perpendicular to the sound
    double soundPressure = sqrt((power*1.2923*343)/(4*pi*distance*distance));
    return 20*log10(soundPressure/(20*pow(10,-6)));
  }
  double decibels_to_watts(double soundPressureLevel, double distance)
  {
    // assumes Normal Temperature and Pressure, aka 20 degrees Celsius,	101.325 kPa
    // here, air density is 1.2923 kg/m^3 and the speed of sound is 343 m/s
    // assumes reference sound pressure (human hearing threshold) is 20 μPa
    // also assumes the listener's eardrums are perfectly perpendicular to the sound
    double soundPressure = pow(10, soundPressureLevel/20) * 20*pow(10,-6);
    return (4*pi*distance*distance*pow(soundPressure,2))/(1.2923*343);
  }
}

void PlayOrPauseSound(emscripten::val event)
{
  audio::play_or_pause();
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

void ResizeCanvas(emscripten::val event)
{
  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  canvas.set("width", emscripten::val(window["innerWidth"].as<double>() * 0.7));
  canvas.set("height", emscripten::val(window["innerHeight"].as<double>() - 80));
  ctx.set("textAlign", emscripten::val("center"));
  ctx.set("textBaseline", emscripten::val("middle"));
  ctx.set("font", emscripten::val("20px Arial"));
}

// width: 60
void DrawResistor(emscripten::val ctx, int x, int y) {
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
/*
  switch(position) {
    case(0):
      ctx.call<void>("lineTo", width*0.3+25+14, height*0.5-14);
      break;
    case(1):
      ctx.call<void>("lineTo", width*0.3+25, height*0.5-20);
      break;
    case(2):
      ctx.call<void>("lineTo", width*0.3+45, height*0.5);
      break;
    default:
      break;
*/
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

void DrawCurrent(emscripten::val ctx, double x, double y, double spacing, double pixelsAtVolume1, double current, std::string label, bool highlight)
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
  ctx.call<void>("moveTo", x, y + spacing);
  double arrowLength = audio::get_initial_volume() * pixelsAtVolume1 * current;
  ctx.call<void>("lineTo", x + arrowLength, y + spacing);
  if (arrowLength > 0)
  {
    ctx.call<void>("moveTo", x + arrowLength - spacing/2.0, y + spacing/2.0);
    ctx.call<void>("lineTo", x + arrowLength, y + spacing);
    ctx.call<void>("lineTo", x + arrowLength - spacing/2.0, y + 3*spacing/2.0);
  } else if (arrowLength < 0) {
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
      break;
    case 1:
      DrawFullCircuit(ctx, false, true, true, false);
      break;
    // case 2:
    //   std::cout << "b3" << std::endl;
    //   break;
    case 4:
      DrawFullCircuit(ctx, true, true, false, false);
      break;
    case 5: {
      double width = canvas["width"].as<double>();
      double height = canvas["height"].as<double>();
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
      DrawCurrent(ctx, width*0.1, height*0.5, 10, width * 0.1, current, "(SLOWED 1000x)", false);
      break;
    }
    case 6:
      DrawFullCircuit(ctx, false, false, false, true);
      break;
    case 7: {
      double width = canvas["width"].as<double>();
      double height = canvas["height"].as<double>();
      DrawCurrent(ctx, width * 0.5, height * 0.2, 10, width * 0.1, audio::get_slowed_current(), "(SLOWED 100x)", false);
      DrawCurrent(ctx, width * 0.7, height * 0.2, 10, width * 0.1, audio::get_current(), "(REAL TIME)", false);
      DrawFullCircuit(ctx, false, false, true, false);
      break;
    }
    case 8: {
      DrawFullCircuit(ctx, false, true, false, false);
      break;
    }
    case 9:
      DrawTwoCircuits(ctx, false, false, false, false);
      break;
    default:
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 200 + 100*sin(FRAME_COUNT/(12*pi)), 150 + 75*sin(FRAME_COUNT/(7.5*pi)), abs(50*sin(FRAME_COUNT/(18*pi))), 0, 2 * pi);
      ctx.call<void>("stroke");
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

void addInductorLabelSet(emscripten::val info, emscripten::val lValue, std::string l, std::string number) {
  addLabel(info, l, "L<sub style=\"font-size:75%;\">" + number +"</sub> = ", "left-label");
  info.call<emscripten::val>("appendChild", lValue);
  addLabel(info, l, "H");
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
      break;
    case (1): {
      emscripten::val lValue = addInputField("lValue", false, 0.1, 0);
      emscripten::val rValue = addInputField("rValue", true, 0.1, 0);
      emscripten::val tValue = addInputField("tValue", true, 0.1, 0);

      addParagraph(info, "The time constant, &#120591, of this system is given by the equation");
      addParagraph(info, "&#120591 = 2L/R");
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
      addLabel(info, "tValue", "&#120591 = ", "left-label");
      info.call<emscripten::val>("appendChild", tValue);
      addLabel(info, "tValue", "s");
      addParagraph(info, "GOAL: &#120591 > 1 s");
      addParagraph(info, "RECOMMENDED: &#120591 > 30 s");
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      disableNextButton();
      break;
    }
    case (2):
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      enableNextButton();
      break;
    case (3):
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      enableNextButton();
      break;
    case (4):
    {
      emscripten::val lValue = addInputField("lValue", false, 0.1, 0);
      emscripten::val cValue = addInputField("cValue", true, 0.1, 0);
      emscripten::val fValue = addInputField("fValue", true, 0.1, 0);

      addLabel(info, "lValue", "L = ", "left-label");
      info.call<emscripten::val>("appendChild", lValue);
      addLabel(info, "lValue", "H");
      addBreak(info);
      addBreak(info);
      addLabel(info, "cValue", "C = ", "left-label");
      info.call<emscripten::val>("appendChild", cValue);
      addLabel(info, "cValue", "F");
      addBreak(info);
      addBreak(info);
      addLabel(info, "fValue", "f = ", "left-label");
      info.call<emscripten::val>("appendChild", fValue);
      addLabel(info, "fValue", "Hz");
      addParagraph(info, "GOAL: f = 440 Hz");
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      enableNextButton();
      break;
    }
    case (5):
      addParagraph(info,
                   "The current that goes through a speaker powers a solenoid (called a \"voice coil\"), which generates magnetic field according to");
      addParagraph(info, "B&#8347 = &#956&#8320nI");
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
      emscripten::val lValue = addInputField("lValue", false, 0.1, 0);
      emscripten::val cValue = addInputField("cValue", true, 0.1, 0);
      emscripten::val vValue = addInputField("vValue", true, 0.1, 0);
      emscripten::val pValue = addInputField("pValue", true, 0.1, 0);
      emscripten::val lpValue = addInputField("lpValue", true, 0.1, 0);

      addLabel(info, "lValue", "L = ", "left-label");
      info.call<emscripten::val>("appendChild", lValue);
      addLabel(info, "lValue", "H");
      addBreak(info);
      addBreak(info);
      addLabel(info, "cValue", "C = ", "left-label");
      info.call<emscripten::val>("appendChild", cValue);
      addLabel(info, "cValue", "F");
      addBreak(info);
      addBreak(info);
      addLabel(info, "vValue", "v = ", "left-label");
      info.call<emscripten::val>("appendChild", vValue);
      addLabel(info, "vValue", "V");
      addBreak(info);
      addBreak(info);
      addLabel(info, "pValue", "P = ", "left-label");
      info.call<emscripten::val>("appendChild", pValue);
      addLabel(info, "pValue", "W");
      addBreak(info);
      addBreak(info);
      addLabel(info, "lpValue", "L<sub style=\"font-size:75%;\">p</sub> = ", "left-label");
      info.call<emscripten::val>("appendChild", lpValue);
      addLabel(info, "lpValue", "dB");
      addParagraph(info, "GOAL: ?");
      if (circuitCompleted) {
        enablePlayButton();
      } else {
        disablePlayButton();
      }
      enableNextButton();
      break;
    }
    case (7): {
      std::string perfectlyEfficient = std::to_string(audio::watts_to_decibels(1, 1));
      std::string extraordinarilyEfficient = std::to_string(audio::decibels_to_watts(105, 1) * 100) + "%";
      std::string veryEfficient = std::to_string(audio::decibels_to_watts(95, 1) * 100) + "%";
      std::string average = std::to_string(audio::decibels_to_watts(88, 1) * 100) + "%";
      std::string veryInefficient = std::to_string(audio::decibels_to_watts(83, 1) * 100) + "%";
      emscripten::val rValue = addInputField("rValue", false, 2, 0, 16, 6);
      emscripten::val efficiencyValue = addInputField("efficiencyValue", true, 0.01);
      emscripten::val sensitivityValue = addInputField("sensitivityValue", false, 0.1, 0, 109.453876, 88);
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
      addLabel(info, "sensitivityValue", "dB<sub style=\"font-size:75%;\">1m</sub> = ", "left-label");
      info.call<emscripten::val>("appendChild", sensitivityValue);
      addLabel(info, "sensitivityValue", "dB");
      addBreak(info);
      addBreak(info);
      addLabel(info, "efficiencyValue", "ŋ = ", "left-label");
      info.call<emscripten::val>("appendChild", efficiencyValue);
      addLabel(info, "efficiencyValue", "% efficiency");
      enablePlayButton();
      enableNextButton();
      break;
    }
    case(8):
    {
      emscripten::val l1Value = addInputField("l1Value", false, 0.1, 0);
      emscripten::val l2Value = addInputField("l2Value", false, 0.1, 0);
      emscripten::val l3Value = addInputField("l3Value", false, 0.1, 0);
      emscripten::val l4Value = addInputField("l4Value", false, 0.1, 0);
      emscripten::val l5Value = addInputField("l5Value", false, 0.1, 0);
      emscripten::val l6Value = addInputField("l6Value", false, 0.1, 0);
      emscripten::val l7Value = addInputField("l7Value", false, 0.1, 0);
      emscripten::val l8Value = addInputField("l8Value", false, 0.1, 0);
      emscripten::val l9Value = addInputField("l9Value", false, 0.1, 0);
      emscripten::val l10Value = addInputField("l10Value", false, 0.1, 0);
      emscripten::val l11Value = addInputField("l11Value", false, 0.1, 0);
      emscripten::val l12Value = addInputField("l12Value", false, 0.1, 0);
      emscripten::val l13Value = addInputField("l13Value", false, 0.1, 0);

      addParagraph(info, "C<sub style=\"font-size:75%;\">4</sub>: 261.63 Hz");
      addInductorLabelSet(info, l1Value, "l1Value", "1");
      addBreak(info);
      addParagraph(info, "C#<sub style=\"font-size:75%;\">4</sub>: 277.18 Hz");
      addInductorLabelSet(info, l2Value, "l2Value", "2");
      addBreak(info);
      addParagraph(info, "D<sub style=\"font-size:75%;\">4</sub>: 293.66 Hz");
      addInductorLabelSet(info, l3Value, "l3Value", "3");
      addBreak(info);
      addParagraph(info, "D#<sub style=\"font-size:75%;\">4</sub>: 311.13 Hz");
      addInductorLabelSet(info, l4Value, "l4Value", "4");
      addBreak(info);
      addParagraph(info, "E<sub style=\"font-size:75%;\">4</sub>: 329.63 Hz");
      addInductorLabelSet(info, l5Value, "l5Value", "5");
      addBreak(info);
      addParagraph(info, "F<sub style=\"font-size:75%;\">4</sub>: 349.23 Hz");
      addInductorLabelSet(info, l6Value, "l6Value", "6");
      addBreak(info);
      enablePlayButton();
      enableNextButton();
      break;
    }
    default:
      printf("page out of range\n");
      break;
  }
}

void RenderSidebar()
{
  static bool nextButtonEnabled = false;
  switch(page) {
    case (0):
      break;
    case (1): {
      emscripten::val inductor = document.call<emscripten::val>("getElementById", emscripten::val("lValue"));
      emscripten::val resistor = document.call<emscripten::val>("getElementById", emscripten::val("rValue"));
      emscripten::val tConstant = document.call<emscripten::val>("getElementById", emscripten::val("tValue"));
      double tV = 0;
      resistor.set("value", emscripten::val(4));
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
        audio::set_time_constant(tV);
      } else if (tV < 1 && nextButtonEnabled) {
        disableNextButton();
        nextButtonEnabled = false;
      }
      break;
    }
    case 4:
    {
      std::vector<double> frequencies{-1};
      if (false) {
        audio::set_frequencies(frequencies);
      }
      break;
    }
    case 7:
    {
      emscripten::val sensitivity = document.call<emscripten::val>("getElementById", emscripten::val("sensitivityValue"));
      emscripten::val efficiency = document.call<emscripten::val>("getElementById", emscripten::val("efficiencyValue"));
      double efficiencyValue = audio::decibels_to_watts(stod(sensitivity["value"].as<std::string>()), 1);
      efficiency.set("value", emscripten::val(efficiencyValue*100));
      double initialVolume = -1;
      if (false) {
        audio::set_initial_volume(initialVolume);
      }
      break;
    }
    case 8:
      circuitCompleted = true;
      break;
    default:
      break;
  }
}

void CloseIntro(emscripten::val event) {
  document.call<emscripten::val>("getElementById", emscripten::val("blur")).call<void>("remove", emscripten::val("mouseup"));
  audio::initialize();
}

void Render()
{
  RenderCanvas();
  RenderSidebar();
}

void StoreData(int page); // forward declaration

extern "C"
{
EMSCRIPTEN_KEEPALIVE
void SelectPage(int i)
{
  page = i;
  InitializePage(page);
  StoreData(page);
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
  localStorage.call<void>("setItem", emscripten::val("timeConstant"), emscripten::val(audio::get_time_constant()));
  localStorage.call<void>("setItem", emscripten::val("initialVolume"), emscripten::val(audio::get_initial_volume()));
  std::string temp = "";
  for (auto& frequency : audio::get_frequencies())
  {
    temp += std::to_string(frequency);
    temp += ",";
  }
  localStorage.call<void>("setItem", emscripten::val("frequencies"), emscripten::val(temp));
}

void RetrieveData()
{
  emscripten::val localStorage = emscripten::val::global("localStorage");
  emscripten::val pageNumber = localStorage.call<emscripten::val>("getItem", emscripten::val("selectedPage"));
  emscripten::val timeConstant = localStorage.call<emscripten::val>("getItem", emscripten::val("timeConstant"));
  emscripten::val initialVolume = localStorage.call<emscripten::val>("getItem", emscripten::val("initialVolume"));
  emscripten::val frequencies = localStorage.call<emscripten::val>("getItem", emscripten::val("frequencies"));
  // checks if there is such a stored value: typeOf will be "object" when the emscripten::val is null
  if (timeConstant.typeOf().as<std::string>() == "string") {
    audio::set_time_constant(std::stod(timeConstant.as<std::string>()));
  } else {
    audio::set_time_constant(1.5);
  }
  if (initialVolume.typeOf().as<std::string>() == "string") {
    audio::set_initial_volume(std::stod(initialVolume.as<std::string>()));
  } else {
    audio::set_initial_volume(0.5);
  }
  if (frequencies.typeOf().as<std::string>() == "string") {
    std::string frequenciesString = frequencies.as<std::string>();
    std::vector<double> temp;
    while (frequenciesString != "")
    {
      int pos = frequenciesString.find(",");
      if (pos == std::string::npos) {break;}
      temp.push_back(std::stod(frequenciesString.substr(0, pos)));
      frequenciesString = frequenciesString.substr(pos+1);
    }
    audio::set_frequencies(temp);
  } else {
    std::vector<double> defaultFrequencies{261.63, 329.63, 392.00}; // C major chord
    audio::set_frequencies(defaultFrequencies);
  }
  if (pageNumber.typeOf().as<std::string>() == "string") {
    SelectPage(std::stoi(pageNumber.as<std::string>()));
  } else {
    SelectPage(0);
  }
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

  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  canvas.set("width", emscripten::val(window["innerWidth"].as<double>() * 0.7));
  canvas.set("height", emscripten::val(window["innerHeight"].as<double>() - 80));
  window.call<void>("addEventListener", emscripten::val("resize"), emscripten::val::module_property("ResizeCanvas"));
  ctx.set("textAlign", emscripten::val("center"));
  ctx.set("textBaseline", emscripten::val("middle"));
  ctx.set("font", emscripten::val("20px Arial"));
  document.call<emscripten::val>("getElementById", emscripten::val("next")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("NextPage"));
  document.call<emscripten::val>("getElementById", emscripten::val("play")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("PlayOrPauseSound"));
  document.call<emscripten::val>("getElementById", emscripten::val("intro-button")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("CloseIntro"));
  RetrieveData();
  StoreData(page);

  // must be the last command in main()
  emscripten_set_main_loop(Render, 0, 1);
  return 0;
}


EMSCRIPTEN_BINDINGS(bindings)
{
  emscripten::function("InteractWithCanvas", InteractWithCanvas);
  emscripten::function("ResizeCanvas", ResizeCanvas);
  emscripten::function("SelectPage", SelectPage);
  emscripten::function("NextPage", NextPage);
  emscripten::function("VolumeControl", audio::volume_control);
  emscripten::function("PlayOrPauseSound", PlayOrPauseSound);
  emscripten::function("CloseIntro", CloseIntro);
}
