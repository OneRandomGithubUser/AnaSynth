#include <emscripten/val.h>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

#include <iostream>
#include <numbers>
#include <cmath>
#include <vector>
#include <optional>

emscripten::val window = emscripten::val::global("window");
emscripten::val document = emscripten::val::global("document");
const double pi = std::numbers::pi;
const double e = std::numbers::e;
static int page;


extern "C"
{
EMSCRIPTEN_KEEPALIVE
void SelectPage(int i)
{
  page = i;
}
}

namespace audio
{
  static emscripten::val globalAudioContext = emscripten::val::global("AudioContext");
  static std::vector<emscripten::val> oscillators;
  // audioContext is allowed to start only after user interactions, so this must only be created when initialized
  static std::optional<emscripten::val> audioContext;
  static std::optional<emscripten::val> gainNode;
  static std::optional<emscripten::val> volumeManager;
  static double TIME_CONSTANT = 1.5; // in seconds
  static double beginTime = 0.0;
  static bool initialized = false;
  static bool playing = false;
  static double initialVolume = 1.5;
  static int timeConstants = 0;
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
  void initialize()
  {
    // because you cannot create audioContext until user interaction with the page, a rule enforced by browsers
    if (!initialized)
    {
      emscripten::val baseAudioContext = globalAudioContext.new_();
      audioContext.emplace(baseAudioContext);
      gainNode.emplace(audioContext.value().call<emscripten::val>("createGain"));
      gainNode.value().call<void>("connect", audioContext.value()["destination"]); // connect gainNode to audio output
    }
    initialized = true;
  }
  void set_vars(std::vector<double>& frequencies, double startingVolume, double timeConstant)
  {
    if (!initialized)
    {
      // This function can be called ONLY after audio::initialize() is called
      // emscripten currently does not have much support for throwing std::exception
      std::cout << "Error: audio::set_vars() called before audio::initialize()\n";
    }
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
    // connect oscillators to audio output
    for (auto &oscillator: oscillators) {
      oscillator.call<void>("connect", gainNode.value());
    }
    // start the oscillators together
    for (auto &oscillator: oscillators) {
      oscillator.call<void>("start");
    }
    initialVolume = startingVolume;
    TIME_CONSTANT = timeConstant;
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
      // fade out in 0.1 seconds - doesn't actually work so, if time leftover, TODO
      gainNode.value()["gain"].call<emscripten::val>("exponentialRampToValueAtTime",
                                                     emscripten::val(0.000001),
                                                     emscripten::val(currentTime.as<double>() + 0.1));
      // pause in 0.1 seconds
      for (auto &oscillator: oscillators)
      {
        oscillator.call<void>("stop", emscripten::val(currentTime.as<double>() + 0.1));
      }
      window.call<void>("clearInterval", volumeManager.value());
      timeConstants = 0;
      playing = false;
    } else {
      // play
      beginTime = audioContext.value()["currentTime"].as<double>();
      // mute sound then go to full volume in 0.05 seconds
      gainNode.value()["gain"].set("value", emscripten::val(0.000001));
      gainNode.value()["gain"].call<emscripten::val>("exponentialRampToValueAtTime",
                                                     emscripten::val(initialVolume),
                                                     emscripten::val(beginTime + 0.05));
      volume_control();
      volumeManager.emplace(
              window.call<emscripten::val>("setInterval", emscripten::val::module_property("VolumeControl"),
                                           emscripten::val(TIME_CONSTANT * 1000)));
      playing = true;
    }
  }
}

void PlayOrPauseSound(emscripten::val event)
{
  audio::initialize();
  std::vector<double> frequencies{261.63, 329.63, 392.00}; // C major chord
  audio::set_vars(frequencies, 0.5, 1.5);
  audio::play_or_pause();
}

void InteractWithCanvas(emscripten::val event)
{
  std::string eventName = event["type"].as<std::string>();
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
  ctx.set("fillStyle", emscripten::val("white"));
}

// width: 60
void DrawResistor(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y);
  ctx.call<void>("lineTo", x+5, y-8);
  ctx.call<void>("lineTo", x+15, y+8);
  ctx.call<void>("lineTo", x+25, y-8);
  ctx.call<void>("lineTo", x+35, y+8);
  ctx.call<void>("lineTo", x+45, y-8);
  ctx.call<void>("lineTo", x+55, y+8);
  ctx.call<void>("lineTo", x+60, y);
  ctx.call<void>("stroke");
}

// width: 50
void DrawCapacitor(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y);
  ctx.call<void>("lineTo", x+20, y);
  ctx.call<void>("moveTo", x+20, y+20);
  ctx.call<void>("lineTo", x+20, y-20);
  ctx.call<void>("moveTo", x+30, y+20);
  ctx.call<void>("lineTo", x+30, y-20);
  ctx.call<void>("moveTo", x+30, y);
  ctx.call<void>("lineTo", x+50, y);
  ctx.call<void>("stroke");
}

// width: 60
void DrawInductor(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y);
  ctx.call<void>("arc", x+10, y, 10, pi, 0, false);
  ctx.call<void>("arc", x+30, y, 10, pi, 0, false);
  ctx.call<void>("arc", x+50, y, 10, pi, 0, false);
  ctx.call<void>("stroke");
}

void DrawSpeaker(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y-5);
  ctx.call<void>("lineTo", x, y+5);
  ctx.call<void>("lineTo", x+20, y+5);
  ctx.call<void>("lineTo", x+20, y-5);
  ctx.call<void>("lineTo", x, y-5);
  ctx.call<void>("lineTo", x, y-5);
  ctx.call<void>("lineTo", x-10, y-20);
  ctx.call<void>("lineTo", x+30, y-20);
  ctx.call<void>("lineTo", x+20, y-5);
  ctx.call<void>("stroke");
}

void DrawBattery(emscripten::val ctx, int x, int y) {
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", x, y);
  ctx.call<void>("lineTo", x+10, y);
  ctx.call<void>("moveTo", x+10, y+10);
  ctx.call<void>("lineTo", x+10, y-10);
  ctx.call<void>("moveTo", x+20, y+20);
  ctx.call<void>("lineTo", x+20, y-20);
  ctx.call<void>("moveTo", x+30, y+10);
  ctx.call<void>("lineTo", x+30, y-10);
  ctx.call<void>("moveTo", x+40, y+20);
  ctx.call<void>("lineTo", x+40, y-20);
  ctx.call<void>("moveTo", x+40, y);
  ctx.call<void>("lineTo", x+50, y);
  ctx.call<void>("stroke");
}

void DrawFullCircuit(emscripten::val ctx) {
  DrawCapacitor(ctx, 170, 400);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 240, 380);
      ctx.call<void>("lineTo", 320, 380);
      ctx.call<void>("stroke");
      DrawInductor(ctx, 320, 380);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 380, 380);
      ctx.call<void>("lineTo", 430, 380);
      ctx.call<void>("lineTo", 430, 200);
      ctx.call<void>("lineTo", 200, 200);
      ctx.call<void>("stroke");
      DrawSpeaker(ctx, 180, 200);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 180, 200);
      ctx.call<void>("lineTo", 130, 200);
      ctx.call<void>("lineTo", 130, 400);
      ctx.call<void>("lineTo", 170, 400);
      ctx.call<void>("lineTo", 170, 320);
      ctx.call<void>("stroke");
      DrawBattery(ctx, 170, 320);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 220, 320);
      ctx.call<void>("lineTo", 220, 380);
      ctx.call<void>("lineTo", 234, 394);
      ctx.call<void>("stroke");
      ctx.set("fillStyle", emscripten::val("black"));
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 220, 400, 2, 0, 2*pi);
      ctx.call<void>("fill");
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 220, 380, 2, 0, 2*pi);
      ctx.call<void>("fill");
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 240, 380, 2, 0, 2*pi);
      ctx.call<void>("fill");
}

void RenderCanvas()
{
  static int FRAME_COUNT = 0;
  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  ctx.set("fillStyle", emscripten::val("white"));
  ctx.call<void>("fillRect", 0, 0, canvas["width"], canvas["height"]);
  switch(page)
  {
    case 0:
      DrawCapacitor(ctx, 170, 400);
      ctx.call<void>("beginPath");
      ctx.call<void>("moveTo", 220, 400);
      ctx.call<void>("lineTo", 320, 400);
      ctx.call<void>("stroke");
      DrawInductor(ctx, 320, 400);
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
      DrawFullCircuit(ctx);
      break;
    // case 2:
    //   std::cout << "b3" << std::endl;
    //   break;
    case 3:
      DrawFullCircuit(ctx);
      break;
    case 5:
      DrawFullCircuit(ctx);
      break;
    case 6:
      DrawFullCircuit(ctx);
      break;
    default:
      ctx.call<void>("fillRect", 0, 0, canvas["width"], canvas["height"]);
      ctx.call<void>("beginPath");
      ctx.call<void>("arc", 200 + 100*sin(FRAME_COUNT/(12*pi)), 150 + 75*sin(FRAME_COUNT/(7.5*pi)), abs(50*sin(FRAME_COUNT/(18*pi))), 0, 2 * pi);
      ctx.call<void>("stroke");
  }
  FRAME_COUNT++;
}

void Render()
{
  RenderCanvas();
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
  ctx.set("fillStyle", emscripten::val("white"));
  window.call<void>("addEventListener", emscripten::val("resize"), emscripten::val::module_property("ResizeCanvas"));
  document.call<emscripten::val>("getElementById", emscripten::val("play")).call<void>("addEventListener", emscripten::val("mouseup"), emscripten::val::module_property("PlayOrPauseSound"));

  // must be the last command in main()
  emscripten_set_main_loop(Render, 0, 1);
  return 0;
}


EMSCRIPTEN_BINDINGS(bindings)
{
  emscripten::function("InteractWithCanvas", InteractWithCanvas);
  emscripten::function("ResizeCanvas", ResizeCanvas);
  emscripten::function("SelectPage", SelectPage);
  emscripten::function("VolumeControl", audio::volume_control);
  emscripten::function("PlayOrPauseSound", PlayOrPauseSound);
}
