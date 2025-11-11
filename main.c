#include <assert.h>
#include <linux/joystick.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <plutovg.h>

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_render.h"
#include "cutils.h"
#include "quickjs-libc.h"
#include "quickjs.h"

// #region Event

static JSClassID js_event_class_id;

static void js_event_finalizer(JSRuntime *rt, JSValue val) {
  SDL_Event *s = JS_GetOpaque(val, js_event_class_id);
  // Note: 's' can be NULL in case JS_SetOpaque() was not called
  js_free_rt(rt, s);
}

static JSValue js_event_ctor(JSContext *ctx, JSValueConst new_target, int argc,
                             JSValueConst *argv) {
  return JS_EXCEPTION;
}

static JSClassDef js_event_class = {
    "Event",
    .finalizer = js_event_finalizer,
};

static JSValue js_event_get_attr(JSContext *ctx, JSValueConst this_val,
                                 int magic) {
  SDL_Event *s = JS_GetOpaque2(ctx, this_val, js_event_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }
  switch (magic) {
  case 0:
    return JS_NewInt32(ctx, s->type);
  // KeyEvent
  case 1: // code
    return JS_NewInt64(ctx, s->key.key);
  case 2: // alt_key
    return JS_NewBool(ctx, (s->key.mod & SDL_KMOD_ALT) != 0);
  case 3: // ctrl_key
    return JS_NewBool(ctx, (s->key.mod & SDL_KMOD_CTRL) != 0);
  case 4: // shift_key
    return JS_NewBool(ctx, (s->key.mod & SDL_KMOD_SHIFT) != 0);
  case 5: // meta_key
    return JS_NewBool(ctx, (s->key.mod & SDL_KMOD_GUI) != 0);
  case 6: // repeat
    return JS_NewBool(ctx, s->key.repeat);
  case 7: // key
    return JS_NewString(ctx, SDL_GetKeyName(s->key.key));

  // MouseEvent
  case 10: // button
    return JS_NewInt32(ctx, s->button.button);
  case 11: // x
    return JS_NewInt32(ctx, s->button.x);
  case 12: // y
    return JS_NewInt32(ctx, s->button.y);

  default:
    return JS_UNDEFINED;
  }
}

static const JSCFunctionListEntry js_event_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("type", js_event_get_attr, NULL, 0),
    // KeyEvent
    JS_CGETSET_MAGIC_DEF("code", js_event_get_attr, NULL, 1),
    JS_CGETSET_MAGIC_DEF("altKey", js_event_get_attr, NULL, 2),
    JS_CGETSET_MAGIC_DEF("ctrlKey", js_event_get_attr, NULL, 3),
    JS_CGETSET_MAGIC_DEF("shiftKey", js_event_get_attr, NULL, 4),
    JS_CGETSET_MAGIC_DEF("metaKey", js_event_get_attr, NULL, 5),
    JS_CGETSET_MAGIC_DEF("repeat", js_event_get_attr, NULL, 6),
    JS_CGETSET_MAGIC_DEF("key", js_event_get_attr, NULL, 7),
    // MouseEvent
    JS_CGETSET_MAGIC_DEF("button", js_event_get_attr, NULL, 10),
    JS_CGETSET_MAGIC_DEF("x", js_event_get_attr, NULL, 11),
    JS_CGETSET_MAGIC_DEF("y", js_event_get_attr, NULL, 12),
};

static int js_event_init(JSContext *ctx) {
  JSValue event_proto, event_class;

  JS_NewClassID(&js_event_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_event_class_id, &js_event_class);

  event_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, event_proto, js_event_proto_funcs,
                             countof(js_event_proto_funcs));

  event_class =
      JS_NewCFunction2(ctx, js_event_ctor, "Event", 0, JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, event_class, event_proto);
  JS_SetClassProto(ctx, js_event_class_id, event_proto);

  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "Event", event_class);
  JS_FreeValue(ctx, global);

  return 0;
}

// #endregion

// #region JSCanvas

typedef struct {
  SDL_Color color;
} Style;

typedef struct {
  int width;
  int height;
  Style fill_style;

  SDL_Window *window;
  SDL_Renderer *renderer;

  SDL_Surface *surface;
  void *pixels; // pointer to the pixel data of the surface
  plutovg_surface_t *plutovg_surface;
  plutovg_canvas_t *plutovg_canvas;

} JSCanvas;

static JSClassID js_canvas_class_id;

static void canvas_finalizer(JSCanvas *s) {
  if (s->renderer != NULL) {
    SDL_DestroyRenderer(s->renderer);
  }
  if (s->window != NULL) {
    SDL_DestroyWindow(s->window);
  }
  if (s->surface != NULL) {
    SDL_DestroySurface(s->surface);
  }
  if (s->pixels != NULL) {
    free(s->pixels);
  }
  if (s->plutovg_surface != NULL) {
    plutovg_surface_destroy(s->plutovg_surface);
  }
  if (s->plutovg_canvas != NULL) {
    plutovg_canvas_destroy(s->plutovg_canvas);
  }
}

static void js_canvas_finalizer(JSRuntime *rt, JSValue val) {
  JSCanvas *s = JS_GetOpaque(val, js_canvas_class_id);
  canvas_finalizer(s);
  /* Note: 's' can be NULL in case JS_SetOpaque() was not called */
  js_free_rt(rt, s);
}

static int canvas_initializer(JSCanvas *s) {
  s->window =
      SDL_CreateWindow("Canvas", s->width, s->height, SDL_WINDOW_RESIZABLE);
  if (!s->window) {
    fprintf(stderr, "SDL could not create window! SDL_Error: %s\n",
            SDL_GetError());
    return 1;
  }
  s->renderer = SDL_CreateRenderer(s->window, NULL);
  if (!s->renderer) {
    fprintf(stderr, "SDL could not create renderer! SDL_Error: %s\n",
            SDL_GetError());
    return 1;
  }

  int width = s->width;
  int height = s->height;
  int pitch = width * 4; // 4 bytes per pixel (RGBA)

  // Allocate memory for pixel data
  void *pixels = malloc(height * pitch);
  assert(pixels != NULL && "Failed to allocate memory for pixel buffer");

  // Fill pixels with a default color (e.g., white with full alpha)
  SDL_memset(pixels, 0xFF, s->height * pitch);

  // Create an SDL surface from the pixel buffer
  SDL_Surface *surface = SDL_CreateSurfaceFrom(
      width, height, SDL_PIXELFORMAT_ARGB8888, pixels, pitch);
  if (!surface) {
    free(pixels);
    fprintf(stderr, "SDL could not create surface! SDL_Error: %s\n",
            SDL_GetError());
    return 1;
  }
  s->pixels = pixels;
  s->surface = surface;

  s->plutovg_surface =
      plutovg_surface_create_for_data(pixels, width, height, pitch);
  if (s->plutovg_surface == NULL) {
    fprintf(stderr, "PlutoVG could not create surface!\n");
    return 1;
  }
  s->plutovg_canvas = plutovg_canvas_create(s->plutovg_surface);
  if (s->plutovg_canvas == NULL) {
    fprintf(stderr, "PlutoVG could not create canvas!\n");
    return 1;
  }

  if (!SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_NONE)) {
    fprintf(stderr, "SDL could not set blend mode! SDL_Error: %s\n",
            SDL_GetError());
    return 1;
  }
  return 0;
}

static JSValue js_canvas_ctor(JSContext *ctx, JSValueConst new_target, int argc,
                              JSValueConst *argv) {
  JSCanvas *s;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  s = js_mallocz(ctx, sizeof(*s));
  if (!s) {
    return JS_EXCEPTION;
  }
  if (JS_ToInt32(ctx, &s->width, argv[0])) {
    goto fail;
  }
  if (JS_ToInt32(ctx, &s->height, argv[1])) {
    goto fail;
  }

  /* using new_target to get the prototype is necessary when the
   class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if (JS_IsException(proto)) {
    goto fail;
  }
  obj = JS_NewObjectProtoClass(ctx, proto, js_canvas_class_id);
  JS_FreeValue(ctx, proto);
  if (JS_IsException(obj)) {
    goto fail;
  }
  if (canvas_initializer(s)) {
    goto fail;
  }
  JS_SetOpaque(obj, s);
  return obj;
fail:
  js_free(ctx, s);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSClassDef js_canvas_class = {
    "Canvas",
    .finalizer = js_canvas_finalizer,
};

static JSValue js_canvas_arc(JSContext *ctx, JSValueConst this_val, int argc,
                             JSValueConst *argv) {
  if (argc != 5 && argc != 6) {
    fprintf(stderr, "canvas.arc() expected 5 or 6 arguments, but got %d\n",
            argc);
    return JS_EXCEPTION;
  }
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }
  double x = 0, y = 0, radius = 0, start_angle = 0, end_angle = 0;
  bool counterclockwise = (argc == 6) ? JS_ToBool(ctx, argv[5]) : false;
  if (JS_ToFloat64(ctx, &x, argv[0]) || JS_ToFloat64(ctx, &y, argv[1]) ||
      JS_ToFloat64(ctx, &radius, argv[2]) ||
      JS_ToFloat64(ctx, &start_angle, argv[3]) ||
      JS_ToFloat64(ctx, &end_angle, argv[4])) {
    return JS_EXCEPTION;
  }

  plutovg_canvas_arc(s->plutovg_canvas, x, y, radius, start_angle, end_angle,
                     counterclockwise);
  return JS_UNDEFINED;
}

static JSValue js_canvas_begin_path(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  if (argc != 0) {
    fprintf(stderr, "canvas.beginPath() expected 0 arguments, but got %d\n",
            argc);
    return JS_EXCEPTION;
  }
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }

  plutovg_canvas_new_path(s->plutovg_canvas);
  return JS_UNDEFINED;
}

static JSValue js_canvas_clear(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc != 0) {
    fprintf(stderr, "canvas.clear() expected 0 arguments, but got %d\n", argc);
    return JS_EXCEPTION;
  }
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }

  SDL_Renderer *renderer = s->renderer;
  if (!SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0)) {
    fprintf(stderr, "SDL could not set draw color! SDL_Error: %s\n",
            SDL_GetError());
    return JS_EXCEPTION;
  }
  if (!SDL_RenderClear(renderer)) {
    fprintf(stderr, "SDL could not clear window! SDL_Error: %s\n",
            SDL_GetError());
    return JS_EXCEPTION;
  }
  plutovg_color_t color = {0, 0, 0, 0};
  plutovg_surface_clear(s->plutovg_surface, &color);
  return JS_UNDEFINED;
}

static JSValue js_canvas_clear_rect(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  if (argc != 4) {
    fprintf(stderr, "canvas.clearRect() expected 4 arguments, but got %d\n",
            argc);
    return JS_EXCEPTION;
  }
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }

  double x = 0, y = 0, width = 0, height = 0;
  if (JS_ToFloat64(ctx, &x, argv[0]) || JS_ToFloat64(ctx, &y, argv[1]) ||
      JS_ToFloat64(ctx, &width, argv[2]) ||
      JS_ToFloat64(ctx, &height, argv[3])) {
    return JS_EXCEPTION;
  }
  SDL_Renderer *renderer = s->renderer;
  if (!SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0)) {
    fprintf(stderr, "SDL could not set draw color! SDL_Error: %s\n",
            SDL_GetError());
    return JS_EXCEPTION;
  }
  SDL_FRect rect = {.x = x, .y = y, .w = width, .h = height};
  if (!SDL_RenderFillRect(renderer, &rect)) {
    fprintf(stderr, "SDL could not fill rect! SDL_Error: %s\n", SDL_GetError());
    return JS_EXCEPTION;
  }
  return JS_UNDEFINED;
}

static JSValue js_canvas_fill(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  if (argc != 0) {
    fprintf(stderr, "canvas.fill() expected 0 arguments, but got %d\n", argc);
    return JS_EXCEPTION;
  }
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }
  SDL_Color color = s->fill_style.color;
  plutovg_canvas_set_rgba(s->plutovg_canvas, color.r / 255.0, color.g / 255.0,
                          color.b / 255.0, color.a / 255.0);
  plutovg_canvas_fill(s->plutovg_canvas);
  return JS_UNDEFINED;
}

static JSValue js_canvas_fill_rect(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  if (argc != 4) {
    fprintf(stderr, "canvas.fillRect() expected 4 arguments, but got %d\n",
            argc);
    return JS_EXCEPTION;
  }
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }
  double x = 0, y = 0, width = 0, height = 0;
  if (JS_ToFloat64(ctx, &x, argv[0]) || JS_ToFloat64(ctx, &y, argv[1]) ||
      JS_ToFloat64(ctx, &width, argv[2]) ||
      JS_ToFloat64(ctx, &height, argv[3])) {
    return JS_EXCEPTION;
  }
  plutovg_canvas_fill_rect(s->plutovg_canvas, x, y, width, height);
  return JS_UNDEFINED;
}

static JSValue js_canvas_get_wh(JSContext *ctx, JSValueConst this_val,
                                int magic) {
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s)
    return JS_EXCEPTION;
  if (magic == 0)
    return JS_NewInt32(ctx, s->width);
  else
    return JS_NewInt32(ctx, s->height);
}

static JSValue js_canvas_poll_event(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }

  SDL_Event event;
  if (SDL_PollEvent(&event)) {
    JSValue event_obj = JS_NewObjectClass(ctx, js_event_class_id);
    if (JS_IsException(event_obj)) {
      return JS_EXCEPTION;
    }
    SDL_Event *event_data = js_mallocz(ctx, sizeof(SDL_Event));
    if (!event_data) {
      JS_FreeValue(ctx, event_obj);
      return JS_EXCEPTION;
    }
    *event_data = event;
    JS_SetOpaque(event_obj, event_data);
    return event_obj;
  } else {
    return JS_UNDEFINED;
  }
}

static JSValue js_canvas_quit(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  return JS_UNDEFINED;
}

static JSValue js_canvas_set_fill_color(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
  if (argc != 3 && argc != 4) {
    fprintf(stderr,
            "canvas.setFillColor() expected 3 or 4 arguments, but got %d\n",
            argc);
    return JS_EXCEPTION;
  }
  int32_t r = 0, g = 0, b = 0, a = 255;
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }

  if (JS_ToInt32(ctx, &r, argv[0]) || JS_ToInt32(ctx, &g, argv[1]) ||
      JS_ToInt32(ctx, &b, argv[2]) ||
      (argc == 4 && JS_ToInt32(ctx, &a, argv[3]))) {
    return JS_EXCEPTION;
  }
  SDL_Color color = (SDL_Color){.r = r, .g = g, .b = b, .a = a};

  s->fill_style.color = color;
  plutovg_canvas_set_rgba(s->plutovg_canvas, color.r / 255.0, color.g / 255.0,
                          color.b / 255.0, color.a / 255.0);
  return JS_UNDEFINED;
}

static JSValue js_canvas_set_global_alpha(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
  if (argc != 1) {
    fprintf(stderr, "canvas.setGlobalAlpha() expected 1 argument, but got %d\n",
            argc);
    return JS_EXCEPTION;
  }
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }
  double alpha = 1.0;
  if (JS_ToFloat64(ctx, &alpha, argv[0])) {
    return JS_EXCEPTION;
  }
  plutovg_canvas_set_opacity(s->plutovg_canvas, alpha);
  return JS_UNDEFINED;
}

static JSValue js_canvas_show(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  JSCanvas *s = JS_GetOpaque2(ctx, this_val, js_canvas_class_id);
  if (!s) {
    return JS_EXCEPTION;
  }
  SDL_Texture *texture = SDL_CreateTextureFromSurface(s->renderer, s->surface);
  SDL_FRect srcrect = {.x = 0, .y = 0, .w = s->width, .h = s->height};
  SDL_FRect dstrect = {.x = 0, .y = 0, .w = s->width, .h = s->height};
  SDL_Renderer *renderer = s->renderer;
  if (!SDL_RenderTexture(renderer, texture, &srcrect, &dstrect)) {
    SDL_DestroyTexture(texture);
    fprintf(stderr, "SDL could not render texture! SDL_Error: %s\n",
            SDL_GetError());
    return JS_EXCEPTION;
  }
  SDL_DestroyTexture(texture);

  if (!SDL_RenderPresent(renderer)) {
    fprintf(stderr, "SDL could not present window! SDL_Error: %s\n",
            SDL_GetError());
    return JS_EXCEPTION;
  }
  return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_canvas_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("width", js_canvas_get_wh, NULL, 0),
    JS_CGETSET_MAGIC_DEF("height", js_canvas_get_wh, NULL, 1),

    JS_CFUNC_DEF("arc", 6, js_canvas_arc),
    JS_CFUNC_DEF("beginPath", 0, js_canvas_begin_path),
    JS_CFUNC_DEF("clear", 0, js_canvas_clear),
    JS_CFUNC_DEF("clearRect", 4, js_canvas_clear_rect),
    JS_CFUNC_DEF("fill", 0, js_canvas_fill),
    JS_CFUNC_DEF("fillRect", 4, js_canvas_fill_rect),
    JS_CFUNC_DEF("pollEvent", 0, js_canvas_poll_event),
    JS_CFUNC_DEF("quit", 0, js_canvas_quit),
    JS_CFUNC_DEF("setFillColor", 4, js_canvas_set_fill_color),
    JS_CFUNC_DEF("setGlobalAlpha", 1, js_canvas_set_global_alpha),
    JS_CFUNC_DEF("show", 0, js_canvas_show),
};

static int js_canvas_init(JSContext *ctx) {
  JSValue canvas_proto, canvas_class;

  JS_NewClassID(&js_canvas_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_canvas_class_id, &js_canvas_class);

  canvas_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, canvas_proto, js_canvas_proto_funcs,
                             countof(js_canvas_proto_funcs));

  canvas_class = JS_NewCFunction2(ctx, js_canvas_ctor, "Canvas", 2,
                                  JS_CFUNC_constructor, 0);
  JS_SetConstructor(ctx, canvas_class, canvas_proto);
  JS_SetClassProto(ctx, js_canvas_class_id, canvas_proto);

  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "Canvas", canvas_class);
  JS_FreeValue(ctx, global);

  return 0;
}

// #endregion

// #region quickjs

static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags) {
  JSValue val;
  int ret;

  if ((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
    /* for the modules, we compile then run to be able to set
       import.meta */
    val = JS_Eval(ctx, buf, buf_len, filename,
                  eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
    if (!JS_IsException(val)) {
      js_module_set_import_meta(ctx, val, TRUE, TRUE);
      val = JS_EvalFunction(ctx, val);
    }
    val = js_std_await(ctx, val);
  } else {
    val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
  }
  if (JS_IsException(val)) {
    js_std_dump_error(ctx);
    ret = -1;
  } else {
    ret = 0;
  }
  JS_FreeValue(ctx, val);
  return ret;
}

static int eval_file(JSContext *ctx, const char *filename, int module,
                     int strict) {
  uint8_t *buf;
  int ret, eval_flags;
  size_t buf_len;

  buf = js_load_file(ctx, &buf_len, filename);
  if (!buf) {
    perror(filename);
    exit(1);
  }

  if (module < 0) {
    module = (has_suffix(filename, ".mjs") ||
              JS_DetectModule((const char *)buf, buf_len));
  }
  if (module) {
    eval_flags = JS_EVAL_TYPE_MODULE;
  } else {
    eval_flags = JS_EVAL_TYPE_GLOBAL;
    if (strict)
      eval_flags |= JS_EVAL_FLAG_STRICT;
  }
  ret = eval_buf(ctx, buf, buf_len, filename, eval_flags);
  js_free(ctx, buf);
  return ret;
}

/* also used to initialize the worker context */
static JSContext *JS_NewCustomContext(JSRuntime *rt) {
  JSContext *ctx;
  ctx = JS_NewContext(rt);
  if (!ctx)
    return NULL;
  /* system modules */
  js_init_module_std(ctx, "std");
  js_init_module_os(ctx, "os");
  return ctx;
}

// #endregion

int main(int argc, char *argv[]) {
  if (!SDL_SetAppMetadata("Canvas", "0.0.1", "com.quickjs.canvas")) {
    fprintf(stderr, "SDL could not to set app metadata! SDL_Error: %s\n",
            SDL_GetError());
    exit(1);
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL could not to be initialized! SDL_Error: %s\n",
            SDL_GetError());
    exit(1);
  }

  JSRuntime *rt = JS_NewRuntime();
  js_std_set_worker_new_context_func(JS_NewCustomContext);
  js_std_init_handlers(rt);
  JSContext *ctx = JS_NewCustomContext(rt);
  if (!ctx) {
    fprintf(stderr, "qjs: cannot allocate JS context\n");
    exit(2);
  }

  /* loader for ES6 modules */
  JS_SetModuleLoaderFunc2(rt, NULL, js_module_loader,
                          js_module_check_attributes, NULL);

  js_std_add_helpers(ctx, 0, NULL);

  js_canvas_init(ctx);
  js_event_init(ctx);

  /* make 'std' and 'os' visible to non module code */
  const char *str = "import * as std from 'std';\n"
                    "import * as os from 'os';\n"
                    "globalThis.std = std;\n"
                    "globalThis.os = os;\n";
  eval_buf(ctx, str, strlen(str), "<input>", JS_EVAL_TYPE_MODULE);
  if (eval_file(ctx, "main.js", -1, 0)) {
    goto fail;
  }
  js_std_loop(ctx);

  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  SDL_Quit();

  return 0;
fail:
  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  SDL_Quit();

  return 1;
}
