#include <emscripten/val.h>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

#include <iostream>
#include <numbers>
#include <cmath>

emscripten::val window = emscripten::val::global("window");
emscripten::val document = emscripten::val::global("document");
const double pi = std::numbers::pi;

void InteractWithCanvas(emscripten::val event)
{
  std::string eventName = event["type"].as<std::string>();
  double pageX = event["pageX"].as<double>();
  double pageY = event["pageY"].as<double>();
  std::cout << eventName << " " << pageX << " " << pageY << "\n";
}

void Render()
{
  static int FRAME_COUNT = 0;
  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  emscripten::val ctx = canvas.call<emscripten::val>("getContext", emscripten::val("2d"));
  ctx.call<void>("clearRect", 0, 0, canvas["width"], canvas["height"]);
  ctx.call<void>("beginPath");
  ctx.call<void>("arc", 200 + 100*sin(FRAME_COUNT/(12*pi)), 150 + 75*sin(FRAME_COUNT/(7.5*pi)), abs(50*sin(FRAME_COUNT/(18*pi))), 0, 2 * pi);
  ctx.call<void>("stroke");
  FRAME_COUNT++;
}

int main() {
  document.call<void>("addEventListener",
                      emscripten::val("mousemove"),
                      emscripten::val::module_property("InteractWithCanvas"));

  emscripten::val canvas = document.call<emscripten::val>("getElementById", emscripten::val("canvas"));
  canvas.set("width", window["innerWidth"]);
  canvas.set("height", window["innerHeight"]);

  // must be the last command in main()
  emscripten_set_main_loop(Render, 0, 1);
}

EMSCRIPTEN_BINDINGS(bindings)
{
  emscripten::function("InteractWithCanvas", InteractWithCanvas);
}