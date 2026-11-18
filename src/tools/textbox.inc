// Tools: what the pages with a text box share. The caret moves by characters (not bytes), and a click puts it between two of them.

size_t tbNextChar(const std::string& v, size_t i) {
    if (i >= v.size()) return v.size();
    i++;
    while (i < v.size() && (static_cast<unsigned char>(v[i]) & 0xC0) == 0x80) i++;
    return i;
}

// The byte offset of the character boundary nearest to x, measured from the start of the text drawn at px pixels.
size_t tbCaretAt(const std::string& v, double px, double x, bool bold = false) {
    size_t best = 0;
    double bestD = 1e9;
    for (size_t i = 0;; i = tbNextChar(v, i)) {
        double w = 0, h = 0;
        measure(v.substr(0, i), px, bold, 0, w, h);
        if (std::fabs(w - x) < bestD) { bestD = std::fabs(w - x); best = i; }
        if (i >= v.size()) break;
    }
    return best;
}
