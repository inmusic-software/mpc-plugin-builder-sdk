// mpc_editor_mac.mm - draw an MPC "Plugin Skins" panel in a desktop VST2 host (macOS).
//
// SPDX-License-Identifier: MIT
//
// See mpc_editor.h for why this exists. In short: on the MPC the host draws the panel from TUI.json;
// on a computer nothing does, so this reads the same file and draws it with AppKit and CoreGraphics.
// No third-party code: Foundation parses the JSON, CoreGraphics decodes the PNGs, CoreText draws the
// labels. That keeps the SDK's "MIT only, no Steinberg code, no dependencies" rules intact.
//
// What TUI.json looks like, as much as matters here:
//
//   pageData
//     componentDefinitions.localComponentDefinitions[]  {key: "knob small", value: <definition>}
//     tabs[]  {tabName, componentDefinition: {backgroundData, componentsData[]}}
//
// Every entry in componentsData has a type, a "handle remapping" and absolute bounds "x y w h". The
// type is either a primitive (below) or the name of a local definition, which is itself a list of
// components - so the tree is resolved by recursion, offsetting bounds and carrying the remapping down.
// A handle remapping maps the name a component asks for, almost always "Data", onto "Parameter N".
//
// The primitives, which is the whole vocabulary across every skin in the SDK:
//
//   Image     imageType "Regular" (one picture) or "FilmStrip" (numFrames stacked vertically)
//   Knob      knobType "FilmStrip" (same again) or "ValueSlider" (draws the parameter's text)
//   Button    onImage / offImage, lit when round(value * (numButtonsInGroup - 1)) == buttonId
//   Label     the parameter's value text, or a fixed string
//   Slider    a plain bar (only the no-kit example skins use it)
//   Focus     the MPC's Q-Link focus rectangle: meaningless with a mouse, so not drawn
//
// Film strips are stacked top to bottom, one frame per row of height (image height / numFrames), and a
// frame is exactly the size of the component's bounds.
//
// NOT DONE YET: conditional visibility (additionalInvalidatingHandles, which Undertow uses to hide the
// second head's marker when Head 2 is off) is ignored, so those components always draw. Overlays (the
// MPC's big edit view on double tap) are not drawn either.

#import <Cocoa/Cocoa.h>

#include "../mpc_editor.h"

#if MPC_HAS_EDITOR

#include <cmath>
#include <string>
#include <vector>

namespace {

// ------------------------------------------------------------------------------------------------
// Parsed skin
// ------------------------------------------------------------------------------------------------

// A component can be tied to a parameter's value: the four RESPONSE curves on Undertow's DELAY page are
// four pictures stacked in the same place, each shown only for its own Filter setting. The MPC calls these
// "additionalInvalidatingHandles"; they read "IndexedEnabling/<index>/<count>/<handle>",
// "RangedEnabling/<min>/<max>/<count>/<handle>" or "NonZero/<handle>".
struct Condition
{
    enum Kind { kIndexed, kRanged, kNonZero };
    Kind kind = kIndexed;
    int  low = 0, high = 0, count = 1, param = -1;

    bool holds(float value) const
    {
        if (param < 0) return true;
        if (kind == kNonZero) return value > 0.0005f;
        const int index = (int)std::lround(value * (count > 1 ? count - 1 : 1));
        return kind == kIndexed ? index == low : (index >= low && index <= high);
    }
};

struct Widget
{
    enum Kind { kPicture, kFilmStrip, kButton, kValueText, kNameText, kStaticText, kBar };
    Kind    kind = kPicture;
    NSImage* image = nil;         // picture, film strip, or a button's lit image
    NSImage* imageOff = nil;      // a button's unlit image
    int     frames = 1;
    int     param = -1;           // index into the plugin's parameters, or -1
    int     buttonId = 0;
    int     buttonsInGroup = 1;
    NSRect  bounds = NSZeroRect;  // absolute, top-left origin, in skin pixels
    NSString* text = nil;         // static text
    CGFloat fontSize = 20.0;
    NSColor* colour = nil;
    NSTextAlignment align = NSTextAlignmentCenter;
    std::vector<Condition> conditions;   // all must hold for the component to be drawn
};

// Something the mouse can work. The skin already says which components are controls: the MPC marks them
// acceptsHWFocus, because those are the ones its Q-Link knobs can grab. Buttons get one each, so a click
// can tell which pill of a row it landed on.
struct Control
{
    enum Kind { kTurn, kPress };
    Kind   kind = kTurn;
    NSRect bounds = NSZeroRect;
    int    param = -1;
    int    buttonId = 0;
    int    buttonsInGroup = 1;
    std::vector<Condition> conditions;   // a hidden control cannot be clicked
};

struct Tab
{
    NSString* name = nil;
    NSColor*  background = nil;
    std::vector<Widget> widgets;
    std::vector<Control> controls;
};

struct Skin
{
    std::vector<Tab> tabs;
    int width = 1280, height = 628;
};

// ------------------------------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------------------------------

// Skin colours are "aarrggbb" hex. "0" means transparent.
NSColor* colourFromString(NSString* s, NSColor* fallback)
{
    if (s == nil || s.length < 8) return fallback;
    unsigned int v = 0;
    if (![[NSScanner scannerWithString:s] scanHexInt:&v]) return fallback;
    const CGFloat a = ((v >> 24) & 0xff) / 255.0, r = ((v >> 16) & 0xff) / 255.0;
    const CGFloat g = ((v >> 8) & 0xff) / 255.0, b = (v & 0xff) / 255.0;
    return [NSColor colorWithSRGBRed:r green:g blue:b alpha:a];
}

// Bounds arrive as "x y w h" in one string.
NSRect rectFromString(NSString* s)
{
    if (s == nil) return NSZeroRect;
    NSArray<NSString*>* parts = [s componentsSeparatedByString:@" "];
    if (parts.count < 4) return NSZeroRect;
    return NSMakeRect(parts[0].doubleValue, parts[1].doubleValue, parts[2].doubleValue, parts[3].doubleValue);
}

// "Parameter 20" -> 20. Anything else -> -1.
int parameterFromHandle(NSString* handle)
{
    if (handle == nil || ![handle hasPrefix:@"Parameter "]) return -1;
    return [handle substringFromIndex:10].intValue;
}

// "0.0", "-6", "+35" yes; "1/8D", "Ping-Pong", "Instant" no.
bool looksNumeric(NSString* s)
{
    if (s.length == 0) return false;
    bool digit = false;
    for (NSUInteger i = 0; i < s.length; ++i)
    {
        const unichar c = [s characterAtIndex:i];
        if (c >= '0' && c <= '9') { digit = true; continue; }
        if (c == '.' || c == ',') continue;
        if ((c == '-' || c == '+') && i == 0) continue;
        return false;
    }
    return digit;
}

NSTextAlignment alignmentFromString(NSString* s)
{
    if (s == nil) return NSTextAlignmentCenter;
    if ([s rangeOfString:@"left" options:NSCaseInsensitiveSearch].location != NSNotFound)
        return NSTextAlignmentLeft;
    if ([s rangeOfString:@"right" options:NSCaseInsensitiveSearch].location != NSNotFound)
        return NSTextAlignmentRight;
    return NSTextAlignmentCenter;
}

// ------------------------------------------------------------------------------------------------
// Parsing
// ------------------------------------------------------------------------------------------------

class SkinReader
{
public:
    SkinReader(NSString* dir) : dir_(dir) {}

    bool read(NSDictionary* page, Skin& out)
    {
        NSDictionary* defsNode = page[@"componentDefinitions"];
        for (NSDictionary* d in defsNode[@"localComponentDefinitions"])
            if ([d[@"key"] isKindOfClass:NSString.class] && [d[@"value"] isKindOfClass:NSDictionary.class])
                definitions_[d[@"key"]] = d[@"value"];

        for (NSDictionary* tabNode in page[@"tabs"])
        {
            Tab tab;
            tab.name = [tabNode[@"tabName"] isKindOfClass:NSString.class] ? tabNode[@"tabName"] : @"";
            NSDictionary* def = tabNode[@"componentDefinition"];
            tab.background = colourFromString(def[@"backgroundData"][@"unfocussed"][@"colour"], NSColor.blackColor);
            addChildren(def, NSZeroPoint, nil, tab.widgets, tab.controls, 0);
            out.tabs.push_back(tab);
            // The tab is as big as its background picture; every skin in the SDK is 1280 x 628.
            for (const Widget& w : tab.widgets)
                if (w.kind == Widget::kPicture && w.bounds.origin.x == 0 && w.bounds.origin.y == 0)
                {
                    out.width = (int)std::fmax((double)out.width, w.bounds.size.width);
                    out.height = (int)std::fmax((double)out.height, w.bounds.size.height);
                }
        }
        return !out.tabs.empty();
    }

private:
    // `inherited` is the remapping of the composite we are inside, so a child asking for "Data" ends up
    // at the parameter the composite was given.
    void addChildren(NSDictionary* definition, NSPoint offset, NSDictionary* inherited,
                     std::vector<Widget>& out, std::vector<Control>& controls, int depth)
    {
        addChildren(definition, offset, inherited, out, controls, depth, std::vector<Condition>());
    }

    void addChildren(NSDictionary* definition, NSPoint offset, NSDictionary* inherited,
                     std::vector<Widget>& out, std::vector<Control>& controls, int depth,
                     const std::vector<Condition>& inheritedConds)
    {
        if (depth > 8) return;   // the format allows nesting; nothing in practice goes past two
        for (NSDictionary* entry in definition[@"componentsData"])
        {
            NSDictionary* cd = entry[@"componentData"];
            if (![cd isKindOfClass:NSDictionary.class]) continue;
            NSString* type = cd[@"type"];
            NSDictionary* data = [cd[@"data"] isKindOfClass:NSDictionary.class] ? cd[@"data"] : @{};
            NSDictionary* boundsNode = entry[@"bounds"];
            NSRect r = rectFromString(boundsNode[@"bounds"]);
            r.origin.x += offset.x;
            r.origin.y += offset.y;

            // The MPC's hardware focus rectangle; there is no Q-Link focus with a mouse.
            if ([type isEqualToString:@"Focus"]) continue;
            if ([boundsNode[@"whenVisible"] isEqualToString:@"WhenFocussed"]) continue;

            NSDictionary* mapping = remappingOf(entry, inherited);

            std::vector<Condition> conds = inheritedConds;
            for (NSString* text in boundsNode[@"additionalInvalidatingHandles"])
            {
                Condition c;
                if (parseCondition(text, mapping, c)) conds.push_back(c);
            }

            NSDictionary* composite = definitions_[type];
            if (composite != nil)
            {
                // A focusable composite is a control: a knob, a fader, a row of pills, a readout.
                if ([boundsNode[@"acceptsHWFocus"] isEqualToString:@"Yes"])
                {
                    Control c;
                    c.kind = Control::kTurn;
                    c.bounds = r;
                    c.conditions = conds;
                    NSArray* map = entry[@"handle remapping"][@"map"];
                    for (NSDictionary* pair in map)
                        if ([pair[@"key"] isEqualToString:@"Data"])
                            c.param = parameterFromHandle(mapping[@"Data"] != nil ? mapping[@"Data"]
                                                                                  : pair[@"value"]);
                    if (c.param >= 0) controls.push_back(c);
                }
                addChildren(composite, r.origin, mapping, out, controls, depth + 1, conds);
                continue;
            }
            Widget w;
            w.conditions = conds;
            if (!buildPrimitive(type, data, r, mapping, w)) continue;
            if (w.kind == Widget::kButton && w.param >= 0)
            {
                Control c;
                c.kind = Control::kPress;
                c.bounds = r;
                c.param = w.param;
                c.buttonId = w.buttonId;
                c.buttonsInGroup = w.buttonsInGroup;
                c.conditions = conds;
                controls.push_back(c);
            }
            out.push_back(w);
        }
    }

    // Compose this entry's own remapping with the one it inherits: "Data" -> "Parameter 20".
    NSDictionary* remappingOf(NSDictionary* entry, NSDictionary* inherited)
    {
        NSArray* map = entry[@"handle remapping"][@"map"];
        if (![map isKindOfClass:NSArray.class] || map.count == 0) return inherited;
        NSMutableDictionary* composed = [NSMutableDictionary dictionary];
        for (NSDictionary* pair in map)
        {
            NSString* key = pair[@"key"], *value = pair[@"value"];
            if (![key isKindOfClass:NSString.class] || ![value isKindOfClass:NSString.class]) continue;
            NSString* resolved = inherited[value];
            composed[key] = resolved != nil ? resolved : value;
        }
        return composed;
    }

    bool parseCondition(NSString* text, NSDictionary* mapping, Condition& out)
    {
        if (![text isKindOfClass:NSString.class]) return false;
        NSArray<NSString*>* parts = [text componentsSeparatedByString:@"/"];
        if (parts.count < 2) return false;
        NSString* handle = parts.lastObject;
        NSString* resolved = mapping[handle];
        out.param = parameterFromHandle(resolved != nil ? resolved : handle);
        if (out.param < 0) return false;
        if ([parts[0] isEqualToString:@"NonZero"]) { out.kind = Condition::kNonZero; return true; }
        if ([parts[0] isEqualToString:@"IndexedEnabling"] && parts.count >= 4)
        {
            out.kind = Condition::kIndexed;
            out.low = out.high = parts[1].intValue;
            out.count = parts[2].intValue;
            return out.count > 0;
        }
        if ([parts[0] isEqualToString:@"RangedEnabling"] && parts.count >= 5)
        {
            out.kind = Condition::kRanged;
            out.low = parts[1].intValue;
            out.high = parts[2].intValue;
            out.count = parts[3].intValue;
            return out.count > 0;
        }
        return false;
    }

    int paramOf(NSDictionary* data, NSDictionary* mapping)
    {
        NSString* handle = data[@"handleName"];
        if (![handle isKindOfClass:NSString.class]) return -1;
        NSString* resolved = mapping[handle];
        return parameterFromHandle(resolved != nil ? resolved : handle);
    }

    void applyTextStyle(NSDictionary* style, Widget& w)
    {
        if (![style isKindOfClass:NSDictionary.class]) return;
        NSDictionary* font = style[@"font"];
        if ([font isKindOfClass:NSDictionary.class] && font[@"height"] != nil)
            w.fontSize = [font[@"height"] doubleValue];
        w.colour = colourFromString(style[@"colour"], NSColor.whiteColor);
        w.align = alignmentFromString(style[@"justification"]);
    }

    bool buildPrimitive(NSString* type, NSDictionary* data, NSRect r, NSDictionary* mapping, Widget& w)
    {
        w.bounds = r;
        w.param = paramOf(data, mapping);

        if ([type isEqualToString:@"Image"])
        {
            NSString* file = data[@"image"];
            if (![file isKindOfClass:NSString.class] || file.length == 0) return false;
            w.image = imageNamed(file);
            if (w.image == nil) return false;
            if ([data[@"imageType"] isEqualToString:@"FilmStrip"])
            {
                w.kind = Widget::kFilmStrip;
                w.frames = [data[@"numFrames"] intValue];
                if (w.frames < 1) w.frames = 1;
            }
            else
                w.kind = Widget::kPicture;
            return true;
        }
        if ([type isEqualToString:@"Knob"])
        {
            if ([data[@"knobType"] isEqualToString:@"ValueSlider"])
            {
                w.kind = Widget::kValueText;
                applyTextStyle(data[@"textStyle"], w);
                return w.param >= 0;
            }
            NSString* file = data[@"filmStrip"];
            if (![file isKindOfClass:NSString.class]) return false;
            w.image = imageNamed(file);
            if (w.image == nil) return false;
            w.kind = Widget::kFilmStrip;
            w.frames = [data[@"numFrames"] intValue];
            if (w.frames < 1) w.frames = 1;
            return true;
        }
        if ([type isEqualToString:@"Button"])
        {
            w.image = imageNamed(data[@"onImage"]);
            w.imageOff = imageNamed(data[@"offImage"]);
            if (w.image == nil && w.imageOff == nil) return false;
            w.kind = Widget::kButton;
            w.buttonId = [data[@"buttonId"] intValue];
            w.buttonsInGroup = [data[@"numButtonsInGroup"] intValue];
            if (w.buttonsInGroup < 1) w.buttonsInGroup = 1;
            return true;
        }
        if ([type isEqualToString:@"Label"])
        {
            applyTextStyle(data[@"textStyle"], w);
            NSString* fixed = data[@"text"];
            NSString* kind = data[@"type"];
            if ([fixed isKindOfClass:NSString.class] && fixed.length > 0)
            {
                w.kind = Widget::kStaticText;
                w.text = fixed;
                return true;
            }
            w.kind = [kind isEqualToString:@"Name"] ? Widget::kNameText : Widget::kValueText;
            return w.param >= 0;
        }
        if ([type isEqualToString:@"Slider"])
        {
            w.kind = Widget::kBar;
            return w.param >= 0;
        }
        return false;
    }

    NSImage* imageNamed(NSString* file)
    {
        if (![file isKindOfClass:NSString.class] || file.length == 0) return nil;
        NSImage* cached = cache_[file];
        if (cached != nil) return cached;
        NSString* path = [dir_ stringByAppendingPathComponent:file];
        NSImage* img = [[NSImage alloc] initWithContentsOfFile:path];
        if (img != nil) cache_[file] = img;
        return img;
    }

    NSString* dir_;
    NSMutableDictionary<NSString*, NSDictionary*>* definitions_ = [NSMutableDictionary dictionary];
    NSMutableDictionary<NSString*, NSImage*>* cache_ = [NSMutableDictionary dictionary];
};

} // namespace

// ------------------------------------------------------------------------------------------------
// The view
// ------------------------------------------------------------------------------------------------

@interface MpcSkinView : NSView
@end

@implementation MpcSkinView
{
    Skin _skin;
    mpc::editor::Host _host;
    int _tab;
    CGFloat _tabBarHeight;
    std::vector<float> _shown;      // the parameter values the picture on screen was drawn from
    int     _dragParam;             // the knob being turned, or -1
    float   _dragStartValue;
    CGFloat _dragStartY;
}

- (instancetype)initWithFrame:(NSRect)frame skin:(const Skin&)skin host:(const mpc::editor::Host&)host
{
    self = [super initWithFrame:frame];
    if (self == nil) return nil;
    _skin = skin;
    _host = host;
    _tab = 0;
    _tabBarHeight = 60.0;
    _shown.assign((size_t)(host.numParams > 0 ? host.numParams : 0), -1.0f);
    _dragParam = -1;
    _dragStartValue = 0.0f;
    _dragStartY = 0.0;
    return self;
}

// TUI.json measures from the top left, so flip the view and the coordinates match one for one.
- (BOOL)isFlipped { return YES; }

// A plugin window in a DAW is usually not the key window. By default AppKit swallows the first
// click into an inactive window - it only raises it - so a control would need clicking twice, and
// whichever control you reached for first would seem dead. Take that click.
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }

- (float)valueOf:(int)param
{
    if (param < 0 || param >= _host.numParams || _host.getParam == nullptr) return 0.0f;
    float v = _host.getParam(_host.ctx, param);
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

- (NSString*)displayOf:(int)param
{
    if (param < 0 || _host.getDisplay == nullptr) return @"";
    char buf[128] = {0};
    _host.getDisplay(_host.ctx, param, buf, (int)sizeof(buf) - 1);
    NSString* s = [NSString stringWithUTF8String:buf];
    if (s == nil) return @"";
    // The plugin reports the number and the unit separately, the way effGetParamDisplay and
    // effGetParamLabel do, and the MPC puts them together on screen. Do the same - unless the
    // plugin already spelled the unit out itself, as a custom format like "150 ms" does.
    if (_host.getUnit != nullptr)
    {
        char unit[32] = {0};
        _host.getUnit(_host.ctx, param, unit, (int)sizeof(unit) - 1);
        NSString* u = [NSString stringWithUTF8String:unit];
        if (u.length > 0 && ![s hasSuffix:u] && looksNumeric(s))
            s = [NSString stringWithFormat:@"%@ %@", s, u];
    }
    return s;
}

- (NSString*)nameOf:(int)param
{
    if (param < 0 || _host.getName == nullptr) return @"";
    char buf[128] = {0};
    _host.getName(_host.ctx, param, buf, (int)sizeof(buf) - 1);
    NSString* s = [NSString stringWithUTF8String:buf];
    return s != nil ? s : @"";
}

- (void)drawText:(NSString*)text in:(const Widget&)w
{
    if (text.length == 0) return;
    NSMutableParagraphStyle* para = [NSMutableParagraphStyle new];
    para.alignment = w.align;
    para.lineBreakMode = NSLineBreakByClipping;
    // The MPC's own fonts are not on a desktop machine; the nearest system face at the same size keeps
    // the layout honest even though the lettering differs.
    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:w.fontSize weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: (w.colour != nil ? w.colour : NSColor.whiteColor),
        NSParagraphStyleAttributeName: para,
    };
    // Centre vertically inside the component's box.
    const NSSize size = [text sizeWithAttributes:attrs];
    NSRect box = w.bounds;
    const CGFloat inset = (box.size.height - size.height) * 0.5;
    box.origin.y += inset > 0 ? inset : 0;
    box.size.height -= inset > 0 ? inset : 0;
    [text drawInRect:box withAttributes:attrs];
}

// Film strips stack their frames down the image, one per row, each the size of the component.
- (void)drawStrip:(NSImage*)image frame:(int)index of:(int)frames in:(NSRect)box
{
    if (image == nil) return;
    const NSSize full = image.size;
    if (full.height <= 0 || frames < 1) return;
    const CGFloat frameHeight = full.height / (CGFloat)frames;
    if (index < 0) index = 0;
    if (index >= frames) index = frames - 1;
    // NSImage measures from the bottom, so frame 0 is the topmost band.
    const NSRect from = NSMakeRect(0, full.height - (CGFloat)(index + 1) * frameHeight, full.width, frameHeight);
    [image drawInRect:box fromRect:from operation:NSCompositingOperationSourceOver fraction:1.0
       respectFlipped:YES hints:nil];
}

- (void)drawRect:(NSRect)dirty
{
    if (_tab < 0 || _tab >= (int)_skin.tabs.size()) return;
    const Tab& tab = _skin.tabs[(size_t)_tab];

    [(tab.background != nil ? tab.background : NSColor.blackColor) setFill];
    NSRectFill(self.bounds);

    for (const Widget& w : tab.widgets)
    {
        if (![self isVisible:w.conditions]) continue;

        switch (w.kind)
        {
            case Widget::kPicture:
                [w.image drawInRect:w.bounds fromRect:NSZeroRect
                          operation:NSCompositingOperationSourceOver fraction:1.0
                     respectFlipped:YES hints:nil];
                break;
            case Widget::kFilmStrip:
            {
                const float v = [self valueOf:w.param];
                [self drawStrip:w.image frame:(int)std::lround(v * (w.frames - 1)) of:w.frames in:w.bounds];
                break;
            }
            case Widget::kButton:
            {
                const float v = [self valueOf:w.param];
                int steps = _host.getSteps != nullptr ? _host.getSteps(_host.ctx, w.param) : 0;
                if (steps < 2) steps = w.buttonsInGroup;
                const int selected = (int)std::lround(v * (steps > 1 ? steps - 1 : 1));
                NSImage* img = (selected == w.buttonId) ? w.image : w.imageOff;
                [img drawInRect:w.bounds fromRect:NSZeroRect
                      operation:NSCompositingOperationSourceOver fraction:1.0
                 respectFlipped:YES hints:nil];
                break;
            }
            case Widget::kValueText:
                [self drawText:[self displayOf:w.param] in:w];
                break;
            case Widget::kNameText:
                [self drawText:[self nameOf:w.param] in:w];
                break;
            case Widget::kStaticText:
                [self drawText:w.text in:w];
                break;
            case Widget::kBar:
            {
                const float v = [self valueOf:w.param];
                // NSRectFill composites with Copy, so a translucent colour does not blend over
                // the panel - it replaces those pixels, alpha and all, punching a hole through
                // the background. On screen that hole shows the host's window behind it, which
                // is why this read as a grey box in a dark DAW and as a white one in a saved
                // image. SourceOver blends it instead, which is what was meant all along.
                [[NSColor colorWithWhite:1.0 alpha:0.15] setFill];
                NSRectFillUsingOperation(w.bounds, NSCompositingOperationSourceOver);
                NSRect fill = w.bounds;
                fill.size.width *= v;
                NSColor* barColour = (w.colour != nil ? w.colour : NSColor.whiteColor);
                [[barColour colorWithAlphaComponent:0.40] setFill];
                NSRectFillUsingOperation(fill, NSCompositingOperationSourceOver);

                // gen_ui.py gives a Slider no Label children of its own, because on the MPC the
                // Slider component draws its own name and value - the skin format calls it "the
                // generic name + value bar". Nothing was drawing that here, so every parameter
                // laid out as a slider came up as an empty box: no name, no reading, just a bar.
                // Every other widget (knob, fader, toggle, radio) ships explicit Labels and so
                // looked right, which is what made this easy to miss.
                if (w.param >= 0)
                {
                    Widget t = w;
                    t.bounds = NSInsetRect(w.bounds, 14.0, 0.0);
                    t.fontSize = 22.0;
                    t.colour = [NSColor colorWithWhite:1.0 alpha:0.72];
                    t.align = NSTextAlignmentLeft;
                    [self drawText:[self nameOf:w.param] in:t];
                    t.colour = NSColor.whiteColor;
                    t.align = NSTextAlignmentRight;
                    [self drawText:[self displayOf:w.param] in:t];
                }
                break;
            }
        }
    }

    [self drawTabBar];
}

- (void)drawTabBar
{
    const NSRect bar = NSMakeRect(0, _skin.height, _skin.width, _tabBarHeight);
    [[NSColor colorWithWhite:0.07 alpha:1.0] setFill];
    NSRectFill(bar);

    const CGFloat w = _skin.width / (CGFloat)_skin.tabs.size();
    for (size_t i = 0; i < _skin.tabs.size(); ++i)
    {
        const BOOL active = ((int)i == _tab);
        const NSRect cell = NSMakeRect(i * w, _skin.height, w, _tabBarHeight);
        if (active)
        {
            [[NSColor colorWithWhite:0.16 alpha:1.0] setFill];
            NSRectFill(cell);
        }
        NSMutableParagraphStyle* para = [NSMutableParagraphStyle new];
        para.alignment = NSTextAlignmentCenter;
        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:19 weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName: active ? NSColor.whiteColor
                                                   : [NSColor colorWithWhite:0.55 alpha:1.0],
            NSParagraphStyleAttributeName: para,
        };
        NSRect text = cell;
        text.origin.y += (_tabBarHeight - 23) * 0.5;
        [_skin.tabs[i].name drawInRect:text withAttributes:attrs];
    }
}

- (BOOL)isVisible:(const std::vector<Condition>&)conditions
{
    for (const Condition& c : conditions)
        if (!c.holds([self valueOf:c.param])) return NO;
    return YES;
}

- (void)setValue:(float)v of:(int)param
{
    if (param < 0 || _host.setParam == nullptr) return;
    _host.setParam(_host.ctx, param, v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
    self.needsDisplay = YES;
}

// Buttons are inside the component that owns them, so the smaller target has to win: walk backwards,
// since a composite is always pushed before the children it contains.
- (const Control*)controlAt:(NSPoint)p
{
    if (_tab < 0 || _tab >= (int)_skin.tabs.size()) return nullptr;
    const std::vector<Control>& controls = _skin.tabs[(size_t)_tab].controls;
    for (size_t i = controls.size(); i-- > 0;)
    {
        const Control& c = controls[i];
        if (!NSPointInRect(p, c.bounds)) continue;
        if (![self isVisible:c.conditions]) continue;
        return &controls[i];
    }
    return nullptr;
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    _dragParam = -1;

    if (p.y >= _skin.height && !_skin.tabs.empty())
    {
        const int hit = (int)(p.x / (_skin.width / (CGFloat)_skin.tabs.size()));
        if (hit >= 0 && hit < (int)_skin.tabs.size() && hit != _tab)
        {
            _tab = hit;
            self.needsDisplay = YES;
        }
        return;
    }

    const Control* c = [self controlAt:p];
    if (c == nullptr) return;
    if (c->kind == Control::kPress)
    {
        int steps = _host.getSteps != nullptr ? _host.getSteps(_host.ctx, c->param) : 0;
        if (steps < 2) steps = c->buttonsInGroup;
        // A button alone in its group is a TOGGLE, whatever the parameter says its step count
        // is. That is how the skin format spells a switch - gen_ui.py emits numButtonsInGroup 1
        // for every bool - so the group size is the authority here and the parameter is not.
        // Asking the parameter instead gets 2 back from any bool, which skips this branch and
        // sends buttonId / (steps - 1) = 1 / 1 = 1.0 on every click: the switch lights on the
        // first tap and can never be cleared. Drawing was unaffected, which is what made it
        // look like the skin rather than the editor.
        if (c->buttonsInGroup <= 1 || steps < 2)
        {
            [self setValue:([self valueOf:c->param] > 0.5f ? 0.0f : 1.0f) of:c->param];
            return;
        }
        [self setValue:(float)c->buttonId / (float)(steps - 1) of:c->param];
        return;
    }
    _dragParam = c->param;
    _dragStartValue = [self valueOf:c->param];
    _dragStartY = p.y;
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam < 0) return;
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    // The view is flipped, so dragging up means a smaller y. A full sweep is about 300 pixels; holding
    // shift stretches that out for fine work, the way a plugin knob usually behaves.
    const CGFloat travel = (event.modifierFlags & NSEventModifierFlagShift) ? 1800.0 : 300.0;
    [self setValue:(_dragStartValue + (float)((_dragStartY - p.y) / travel)) of:_dragParam];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = -1;
}

// A scroll over a control nudges it, which is what people try first.
- (void)scrollWheel:(NSEvent*)event
{
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    const Control* c = [self controlAt:p];
    if (c == nullptr || c->kind != Control::kTurn) return;
    const CGFloat travel = (event.modifierFlags & NSEventModifierFlagShift) ? 1800.0 : 300.0;
    [self setValue:([self valueOf:c->param] + (float)(event.scrollingDeltaY / travel)) of:c->param];
}

// The host calls this on its UI thread; repaint only when something the panel shows has actually moved.
- (void)refresh
{
    if (_host.getParam == nullptr) return;
    BOOL changed = NO;
    for (size_t i = 0; i < _shown.size(); ++i)
    {
        const float v = _host.getParam(_host.ctx, (int)i);
        if (v != _shown[i]) { _shown[i] = v; changed = YES; }
    }
    if (changed) self.needsDisplay = YES;
}

@end

// ------------------------------------------------------------------------------------------------
// The C++ entry points
// ------------------------------------------------------------------------------------------------

namespace {

NSDictionary* loadPage(const char* skinDir)
{
    if (skinDir == nullptr || skinDir[0] == '\0') return nil;
    NSString* dir = [NSString stringWithUTF8String:skinDir];
    NSData* data = [NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:@"TUI.json"]];
    if (data == nil) return nil;
    NSDictionary* root = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if (![root isKindOfClass:NSDictionary.class]) return nil;
    NSDictionary* page = root[@"pageData"];
    return [page isKindOfClass:NSDictionary.class] ? page : nil;
}

struct EditorHandle
{
    MpcSkinView* view;
};

} // namespace

namespace mpc {
namespace editor {

bool size(const char* skinDir, int* width, int* height)
{
    @autoreleasepool
    {
        NSDictionary* page = loadPage(skinDir);
        if (page == nil) return false;
        Skin skin;
        SkinReader reader([NSString stringWithUTF8String:skinDir]);
        if (!reader.read(page, skin)) return false;
        if (width) *width = skin.width;
        if (height) *height = skin.height + 60;   // the tab bar we draw underneath
        return true;
    }
}

void* open(const char* skinDir, void* parentView, const Host& host)
{
    @autoreleasepool
    {
        NSDictionary* page = loadPage(skinDir);
        if (page == nil) return nullptr;
        Skin skin;
        SkinReader reader([NSString stringWithUTF8String:skinDir]);
        if (!reader.read(page, skin)) return nullptr;

        const NSRect frame = NSMakeRect(0, 0, skin.width, skin.height + 60);
        MpcSkinView* view = [[MpcSkinView alloc] initWithFrame:frame skin:skin host:host];
        if (view == nil) return nullptr;

        NSView* parent = (__bridge NSView*)parentView;
        if (![parent isKindOfClass:NSView.class]) return nullptr;
        [parent addSubview:view];

        EditorHandle* h = new EditorHandle();
        h->view = view;
        return h;
    }
}

void close(void* handle)
{
    @autoreleasepool
    {
        EditorHandle* h = static_cast<EditorHandle*>(handle);
        if (h == nullptr) return;
        [h->view removeFromSuperview];
        h->view = nil;
        delete h;
    }
}

void idle(void* handle)
{
    @autoreleasepool
    {
        EditorHandle* h = static_cast<EditorHandle*>(handle);
        if (h != nullptr) [h->view refresh];
    }
}

} // namespace editor
} // namespace mpc

#endif // MPC_HAS_EDITOR
