// mpc_editor.h - the optional desktop editor: drawing the MPC's own skin in a desktop VST2 host.
//
// SPDX-License-Identifier: MIT
//
// On the MPC the plugin never draws anything. The MPC reads "Plugin Skins/TUI.json" out of the plugin
// folder and renders the panel itself, while the plugin only answers questions about its parameters.
// A desktop host does no such thing, so to show the same panel on a computer somebody has to read that
// file and draw it. That is what this is: a renderer for the MPC skin format, built only into desktop
// builds (`tools/build.py <project> --target desktop`) and compiled out everywhere else.
//
// Reading TUI.json rather than the layout script that generated it is deliberate. TUI.json is the file
// the MPC itself renders, and every plugin has one - hand-written like Undertow's, or generated from
// ui.json by tools/gen_ui.py - so one renderer serves them all and the desktop panel cannot drift away
// from what the hardware shows.
//
// Only macOS is implemented. mpc_plugin.h declares no editor at all unless MPC_DESKTOP_EDITOR is
// defined and a platform backend exists, so nothing here affects an MPC build.
//
// The interface is deliberately plain C-ish structs: the Objective-C++ backend never sees mpc::Plugin.

#ifndef MPC_EDITOR_H
#define MPC_EDITOR_H

#if defined(MPC_DESKTOP_EDITOR) && defined(__APPLE__)
  #define MPC_HAS_EDITOR 1
#else
  #define MPC_HAS_EDITOR 0
#endif

#if MPC_HAS_EDITOR

namespace mpc {
namespace editor {

// What the view needs from the plugin. Every call carries `ctx` back, so the backend holds no C++ types.
struct Host
{
    void* ctx = nullptr;
    int   numParams = 0;
    float (*getParam)(void* ctx, int index) = nullptr;            // 0..1
    void  (*setParam)(void* ctx, int index, float value) = nullptr;
    void  (*getDisplay)(void* ctx, int index, char* out, int cap) = nullptr;   // "1/8D", "-6.0 dB", ...
    void  (*getName)(void* ctx, int index, char* out, int cap) = nullptr;
    void  (*getUnit)(void* ctx, int index, char* out, int cap) = nullptr;    // "dB", "Hz", "%", ...
    int   (*getSteps)(void* ctx, int index) = nullptr;   // discrete values (2 for a switch), 0 if continuous
};

// The panel's size, read from the skin. False if the skin cannot be found or parsed, in which case the
// plugin should report no editor at all rather than open an empty window.
bool size(const char* skinDir, int* width, int* height);

// Create the view and add it to the host's parent window. Returns an opaque handle, or null on failure.
void* open(const char* skinDir, void* parentView, const Host& host);

void close(void* handle);

// Called from the host's UI thread. Repaints if any parameter the skin shows has moved.
void idle(void* handle);

} // namespace editor
} // namespace mpc

#endif // MPC_HAS_EDITOR
#endif // MPC_EDITOR_H
