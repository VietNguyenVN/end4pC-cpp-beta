#ifndef SHAPES_H
#define SHAPES_H

// The Material shape outlines of assets/shapes.txt: a name, then bezier points in a unit square (a start point and curves of six numbers).

#include <cairo.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace shapes {

inline const std::map<std::string, std::vector<double>>& table(const std::string& assetsDir) {
    static const auto all = [&] {
        std::map<std::string, std::vector<double>> out;
        std::ifstream f(assetsDir + "/shapes.txt");
        std::string line;
        while (std::getline(f, line)) {
            std::stringstream ss(line);
            std::string name;
            ss >> name;
            std::vector<double> nums;
            for (double v; ss >> v;) nums.push_back(v);
            if (!name.empty() && nums.size() >= 8 && (nums.size() - 2) % 6 == 0) out[name] = std::move(nums);
        }
        return out;
    }();
    return all;
}

// Adds the outline to the path of cr, scaled to size and with its corner at (x, y). Does nothing for an unknown name.
inline void path(cairo_t* cr, const std::map<std::string, std::vector<double>>& all, const std::string& name, double x, double y, double size) {
    const auto it = all.find(name);
    if (it == all.end()) return;
    const auto& n = it->second;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x + n[0] * size, y + n[1] * size);
    for (size_t i = 2; i + 5 < n.size(); i += 6)
        cairo_curve_to(cr, x + n[i] * size, y + n[i + 1] * size, x + n[i + 2] * size, y + n[i + 3] * size, x + n[i + 4] * size, y + n[i + 5] * size);
    cairo_close_path(cr);
}

}  // namespace shapes

#endif
