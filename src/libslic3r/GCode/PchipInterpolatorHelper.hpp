// [INTENT] PchipInterpolatorHelper implements monotone piecewise-cubic interpolation for the few
// G-code features that need a smooth curve through sparse calibration samples without the overshoot
// of a generic spline. It converts sorted (x, y) sample pairs into one derivative per knot, then
// evaluates a Hermite segment at query time.
//
// [STATE] setData() replaces the entire sample set and recomputes all cached step widths, segment
// slopes, and knot derivatives. interpolate() is read-only after that preprocessing step.
//
// [MEMORY] Owns its sample and coefficient vectors by value; no external storage is referenced.
//
// [HAZARD] Duplicate x values collapse h(i) to zero and would produce division by zero in delta().
// The helper validates count parity but does not reject duplicate abscissas.

#ifndef PCHIPINTERPOLATORHELPER_HPP
#define PCHIPINTERPOLATORHELPER_HPP

#include <vector>

class PchipInterpolatorHelper {
public:
    PchipInterpolatorHelper() = default;
    PchipInterpolatorHelper(const std::vector<double>& x, const std::vector<double>& y);
    void setData(const std::vector<double>& x, const std::vector<double>& y);
    double interpolate(double xi) const;

private:
    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<double> h_;
    std::vector<double> delta_;
    // [STATE] d_[i] stores the monotone-preserving derivative at knot i chosen by the Fritsch-
    // Carlson weighted-harmonic-mean rule.
    std::vector<double> d_;
    void computePCHIP();
    void sortData();
    double h(int i) const { return x_[i+1] - x_[i]; }
    double delta(int i) const { return (y_[i+1] - y_[i]) / h(i); }
};

#endif // PCHIPINTERPOLATORHELPER_HPP
