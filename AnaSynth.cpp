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
    ctx.set("strokeStyle", emscripten::val("Black"));
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
  double height = ctx["canvas"]["width"].as<double>();
  DrawCapacitor(ctx, width*0.3, height*0.5, highlightCapacitor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3+25+20, height*0.5);
  ctx.call<void>("lineTo", width*0.7-40, height*0.5);
  ctx.call<void>("stroke");
  DrawInductor(ctx, width*0.7, height*0.5, highlightInductor);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.7+40, height*0.5);
  ctx.call<void>("lineTo", width*0.9, height*0.5);
  ctx.call<void>("lineTo", width*0.9, height*0.2);
  ctx.call<void>("lineTo", width*0.3+10, height*0.2);
  ctx.call<void>("stroke");
  DrawSpeaker(ctx, width*0.3, height*0.2, highlightSpeaker);
  ctx.call<void>("beginPath");
  ctx.call<void>("moveTo", width*0.3-10, height*0.2);
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
  ctx.call<void>("lineTo", width*0.3+25+14, height*0.5-14);
  ctx.call<void>("stroke");
  ctx.set("fillStyle", emscripten::val("black"));
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", height*0.3+25, height*0.5, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", height*0.3+25, height*0.5-20, 2, 0, 2*pi);
  ctx.call<void>("fill");
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", height*0.3+45, height*0.5, 2, 0, 2*pi);
  ctx.call<void>("fill");
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
  // two interpolation points over time, then r, g, b, startX, startY, endX, endY for the linear gradient
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
    case 3:
      DrawFullCircuit(ctx, true, true, false, false);
      break;
    case 4:
      break;
    case 5:
      DrawFullCircuit(ctx, false, false, false, true);
      break;
    case 6:
      DrawFullCircuit(ctx, false, false, true, false);
      break;
    default:
      ctx.call<void>("fillRect", 0, 0, canvas["width"], canvas["height"]);
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
}

emscripten::val addInputField(const char* id) 
{
  emscripten::val field = document.call<emscripten::val>("createElement", emscripten::val("input"));
  field.set("id", emscripten::val(id));
  field.set("type", emscripten::val("number"));
  return field;
}

emscripten::val addInputField(const char* id, bool disabled) 
{
  emscripten::val field = document.call<emscripten::val>("createElement", emscripten::val("input"));
  field.set("id", emscripten::val(id));
  field.set("type", emscripten::val("number"));
  field.set("disabled", emscripten::val(disabled));
  return field;
}

void addBr(emscripten::val sidebar) {
  sidebar.call<void>("appendChild", document.call<emscripten::val>("createElement", emscripten::val("br")));
}

void addP(emscripten::val sidebar, const char* s) {
  emscripten::val p = document.call<emscripten::val>("createElement", emscripten::val("p"));
  p.set("innerHTML", s);
  sidebar.call<void>("appendChild", p);
}

void addLabel(emscripten::val sidebar, const char* f, const char* s) {
  emscripten::val l = document.call<emscripten::val>("createElement", emscripten::val("label"));
  l.set("htmlFor", f);
  l.set("innerHTML", s);
  sidebar.call<void>("appendChild", l);
}

void InitPage(int i)
{
  emscripten::val sidebar = document.call<emscripten::val>("getElementById", emscripten::val("sidebar"));
  sidebar.set("innerHTML", "");
  switch(i)
  {
    case(0):
      addNextButton(sidebar);
      break;
    case(1):
    {
      emscripten::val lValue = addInputField("lValue");
      emscripten::val rValue = addInputField("rValue");
      emscripten::val tValue = addInputField("tValue", true);

      addLabel(sidebar, "lValue", "L = ");
      sidebar.call<emscripten::val>("appendChild", lValue);
      addLabel(sidebar, "lValue", "F");
      addBr(sidebar);
      addBr(sidebar);
      addLabel(sidebar, "rValue", "R = ");
      sidebar.call<emscripten::val>("appendChild", rValue);
      addLabel(sidebar, "rValue", "&#8486");
      addBr(sidebar);
      addBr(sidebar);
      addLabel(sidebar, "tValue", "&#120591 = ");
      sidebar.call<emscripten::val>("appendChild", tValue);
      addLabel(sidebar, "tValue", "s");
      addPlayButton(sidebar);
      break;
    }
    case(2):
      addNextButton(sidebar);
      break;
    case(3):
      addPlayButton(sidebar);
      addNextButton(sidebar);
      break;
    case(4):
      addNextButton(sidebar);
      break;
    case(5):
      addPlayButton(sidebar);
      addNextButton(sidebar);
      break;
    case(6):
      addPlayButton(sidebar);
      addNextButton(sidebar);
      break;
    default:
      printf("page out of range\n");
      break;
  }
}

void RenderSidebar()
{
  switch(page)
  {
    case(0):
      break;
    case(1):
    {
      emscripten::val inductor = document.call<emscripten::val>("getElementById", emscripten::val("lValue"));
      emscripten::val resistor = document.call<emscripten::val>("getElementById", emscripten::val("rValue"));
      emscripten::val tConstant = document.call<emscripten::val>("getElementById", emscripten::val("tValue"));
      double tV = 0;
      if(inductor["value"].as<std::string>() != "" && resistor["value"].as<std::string>() != "") {
        tV = 2 * stod(inductor["value"].as<std::string>()) / stod(resistor["value"].as<std::string>());
        tConstant.set("value", emscripten::val(tV));
      }
      
      emscripten::val sidebar = document.call<emscripten::val>("getElementById", emscripten::val("sidebar"));
      emscripten::val next = document.call<emscripten::val>("getElementById", emscripten::val("next"));
      if(next == emscripten::val::null() && tV > 1) {
        addNextButton(sidebar);
      }
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
  InitPage(page);
}
void NextPage(emscripten::val event)
{
  page++;
  InitPage(page);
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
  ctx.set("fillStyle", emscripten::val("white"));
  window.call<void>("addEventListener", emscripten::val("resize"), emscripten::val::module_property("ResizeCanvas"));

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
}
