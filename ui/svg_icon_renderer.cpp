#include "markdownmay/ui/svg_icon_renderer.hpp"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <mutex>
#include <string_view>
#include <vector>

namespace markdownmay::ui {
namespace {
using Point = Gdiplus::PointF;

struct Reader final {
    std::string_view text;
    std::size_t at{};
    void skip() { while (at < text.size() && (text[at] == ' ' || text[at] == ',' ||
        text[at] == '\r' || text[at] == '\n' || text[at] == '\t')) ++at; }
    bool number(float& value) {
        skip();
        if (at >= text.size() || (!std::isdigit(static_cast<unsigned char>(text[at])) &&
            text[at] != '-' && text[at] != '+' && text[at] != '.')) return false;
        const auto first = text.data() + at;
        char* end{};
        value = std::strtof(first, &end);
        if (end == first) return false;
        at = static_cast<std::size_t>(end - text.data());
        return true;
    }
};

void AddArc(Gdiplus::GraphicsPath& path, Point from, float rx, float ry,
        float rotation, bool large, bool sweep, Point to) {
    if (rx == 0 || ry == 0 || (from.X == to.X && from.Y == to.Y)) {
        path.AddLine(from, to); return;
    }
    rx = std::abs(rx); ry = std::abs(ry);
    const double phi = rotation * 3.14159265358979323846 / 180.0;
    const double cs = std::cos(phi), sn = std::sin(phi);
    const double dx = (from.X - to.X) / 2.0, dy = (from.Y - to.Y) / 2.0;
    const double xp = cs * dx + sn * dy, yp = -sn * dx + cs * dy;
    double scale = xp * xp / (rx * rx) + yp * yp / (ry * ry);
    if (scale > 1) { scale = std::sqrt(scale); rx *= static_cast<float>(scale); ry *= static_cast<float>(scale); }
    const double numerator = (rx * rx * ry * ry) - (rx * rx * yp * yp) - (ry * ry * xp * xp);
    const double denominator = (rx * rx * yp * yp) + (ry * ry * xp * xp);
    const double factor = (large == sweep ? -1.0 : 1.0) *
        std::sqrt((std::max)(0.0, numerator / denominator));
    const double cxp = factor * rx * yp / ry, cyp = factor * -ry * xp / rx;
    const double cx = cs * cxp - sn * cyp + (from.X + to.X) / 2.0;
    const double cy = sn * cxp + cs * cyp + (from.Y + to.Y) / 2.0;
    auto angle = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const double ux = (xp - cxp) / rx, uy = (yp - cyp) / ry;
    const double vx = (-xp - cxp) / rx, vy = (-yp - cyp) / ry;
    double start = std::atan2(uy, ux), delta = angle(ux, uy, vx, vy);
    if (!sweep && delta > 0) delta -= 2 * 3.14159265358979323846;
    if (sweep && delta < 0) delta += 2 * 3.14159265358979323846;
    const int pieces = (std::max)(1, static_cast<int>(std::ceil(std::abs(delta) / (3.14159265358979323846 / 2))));
    const double step = delta / pieces;
    Point current = from;
    for (int i = 0; i < pieces; ++i) {
        const double a = start + i * step, b = a + step;
        const double k = 4.0 / 3.0 * std::tan((b - a) / 4.0);
        auto map = [&](double x, double y) { return Point{
            static_cast<float>(cx + rx * (cs * x - sn * y)),
            static_cast<float>(cy + ry * (sn * x + cs * y))}; };
        const auto end = map(std::cos(b), std::sin(b));
        const auto c1 = map(std::cos(a) - k * std::sin(a), std::sin(a) + k * std::cos(a));
        const auto c2 = map(std::cos(b) + k * std::sin(b), std::sin(b) - k * std::cos(b));
        path.AddBezier(current, c1, c2, end); current = end;
    }
}

void ParsePath(std::string_view data, Gdiplus::GraphicsPath& path) {
    Reader reader{data}; char command{}; Point current{}, start{}, control{};
    while (reader.at < data.size()) {
        reader.skip(); if (reader.at >= data.size()) break;
        if (std::isalpha(static_cast<unsigned char>(data[reader.at]))) command = data[reader.at++];
        const bool relative = std::islower(static_cast<unsigned char>(command));
        const char op = static_cast<char>(std::tolower(static_cast<unsigned char>(command)));
        if (op == 'z') { path.CloseFigure(); current = start; command = 0; continue; }
        float a{}, b{}, c{}, d{}, e{}, f{}, g{};
        if (op == 'm' || op == 'l') {
            if (!reader.number(a) || !reader.number(b)) break;
            Point next{a, b}; if (relative) { next.X += current.X; next.Y += current.Y; }
            if (op == 'm') { path.StartFigure(); start = next; command = relative ? 'l' : 'L'; }
            else path.AddLine(current, next);
            current = next;
        } else if (op == 'h') {
            if (!reader.number(a)) break; Point next{relative ? current.X + a : a, current.Y};
            path.AddLine(current, next); current = next;
        } else if (op == 'v') {
            if (!reader.number(a)) break; Point next{current.X, relative ? current.Y + a : a};
            path.AddLine(current, next); current = next;
        } else if (op == 'c') {
            if (!reader.number(a)||!reader.number(b)||!reader.number(c)||!reader.number(d)||!reader.number(e)||!reader.number(f)) break;
            Point c1{a,b}, c2{c,d}, next{e,f}; if (relative) {
                c1.X+=current.X; c1.Y+=current.Y; c2.X+=current.X; c2.Y+=current.Y;
                next.X+=current.X; next.Y+=current.Y; }
            path.AddBezier(current,c1,c2,next); control=c2; current=next;
        } else if (op == 's') {
            if (!reader.number(a)||!reader.number(b)||!reader.number(c)||!reader.number(d)) break;
            Point c1{2*current.X-control.X,2*current.Y-control.Y}, c2{a,b}, next{c,d};
            if (relative) { c2.X+=current.X; c2.Y+=current.Y; next.X+=current.X; next.Y+=current.Y; }
            path.AddBezier(current,c1,c2,next); control=c2; current=next;
        } else if (op == 'a') {
            if (!reader.number(a)||!reader.number(b)||!reader.number(c)||!reader.number(d)||!reader.number(e)||!reader.number(f)||!reader.number(g)) break;
            Point next{f,g}; if (relative) { next.X+=current.X; next.Y+=current.Y; }
            AddArc(path,current,a,b,c,d!=0,e!=0,next); current=next;
        } else break;
    }
}

std::vector<std::string_view> Paths(std::string_view svg) {
    std::vector<std::string_view> paths; std::size_t at{};
    while ((at = svg.find("<path", at)) != std::string_view::npos) {
        const auto end = svg.find('>', at); if (end == std::string_view::npos) break;
        const auto d = svg.find(" d=\"", at);
        if (d != std::string_view::npos && d < end) {
            const auto first = d + 4, last = svg.find('"', first);
            if (last < end && svg.substr(at, end-at).find("stroke=\"none\"") == std::string_view::npos)
                paths.push_back(svg.substr(first, last-first));
        }
        at = end + 1;
    }
    return paths;
}
}

bool SvgIconRenderer::draw(HDC dc, const RECT& bounds, std::uint16_t resource_id,
        COLORREF color, UINT dpi) {
    static_cast<void>(dpi);
    const auto resource = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) return false;
    const auto bytes = LoadResource(GetModuleHandleW(nullptr), resource);
    const auto data = static_cast<const char*>(LockResource(bytes));
    const auto size = SizeofResource(GetModuleHandleW(nullptr), resource);
    if (!data || !size) return false;
    static std::once_flag started;
    static ULONG_PTR token{};
    std::call_once(started, [] { Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&token, &input, nullptr); });
    Gdiplus::Graphics graphics(dc); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float icon = static_cast<float>((std::min)(bounds.right-bounds.left, bounds.bottom-bounds.top));
    const float scale = icon / 24.0f;
    Gdiplus::Matrix transform(scale, 0, 0, scale,
        bounds.left + ((bounds.right-bounds.left)-icon)/2.0f,
        bounds.top + ((bounds.bottom-bounds.top)-icon)/2.0f);
    graphics.SetTransform(&transform);
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)),
        2.0f);
    pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound); pen.SetLineJoin(Gdiplus::LineJoinRound);
    for (const auto source : Paths(std::string_view(data, size))) {
        Gdiplus::GraphicsPath path; ParsePath(source, path); graphics.DrawPath(&pen, &path);
    }
    return true;
}

}  // namespace markdownmay::ui
