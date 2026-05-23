#pragma once
#include "components/component.h"
#include <optional>
#include <stdexcept>
#include <string>

namespace CircuitEngine {

// ── VCVS ──────────────────────────────────────────────────────────
class VCVS : public Component {
public:
    VCVS(std::string name,
         std::optional<int> n_p, std::optional<int> n_m,
         std::optional<int> nc_p, std::optional<int> nc_m,
         double gain)
        : Component(std::move(name)), n_p_(n_p), n_m_(n_m),
          nc_p_(nc_p), nc_m_(nc_m), A_(gain) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        int k = getIndex(ctx);
        if (n_p_) { A(n_p_.value(), k) += 1.0; A(k, n_p_.value()) += 1.0; }
        if (n_m_) { A(n_m_.value(), k) -= 1.0; A(k, n_m_.value()) -= 1.0; }
        if (nc_p_) A(k, nc_p_.value()) -= A_;
        if (nc_m_) A(k, nc_m_.value()) += A_;
    }

    std::string type_name() const override { return "VCVS"; }

private:
    std::optional<int> n_p_, n_m_, nc_p_, nc_m_;
    double A_;
    int getIndex(const StampContext& ctx) const {
        if (!ctx.vs_index || !ctx.vs_index->count(name_))
            throw std::runtime_error(name_ + ": VCVS not indexed");
        return ctx.vs_index->at(name_);
    }
};

// ── VCCS ──────────────────────────────────────────────────────────
class VCCS : public Component {
public:
    VCCS(std::string name,
         std::optional<int> n_p, std::optional<int> n_m,
         std::optional<int> nc_p, std::optional<int> nc_m,
         double Gm)
        : Component(std::move(name)), n_p_(n_p), n_m_(n_m),
          nc_p_(nc_p), nc_m_(nc_m), Gm_(Gm) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        // VCCS: I = Gm*(V_nc_p - V_nc_m) flowing from n_p to n_m
        // KCL contribution: current leaves n_p, enters n_m
        if (n_p_ && nc_p_) A(n_p_.value(), nc_p_.value()) -= Gm_;
        if (n_p_ && nc_m_) A(n_p_.value(), nc_m_.value()) += Gm_;
        if (n_m_ && nc_p_) A(n_m_.value(), nc_p_.value()) += Gm_;
        if (n_m_ && nc_m_) A(n_m_.value(), nc_m_.value()) -= Gm_;
    }

    std::string type_name() const override { return "VCCS"; }

private:
    std::optional<int> n_p_, n_m_, nc_p_, nc_m_;
    double Gm_;
};

// ── CCVS ──────────────────────────────────────────────────────────
class CCVS : public Component {
public:
    CCVS(std::string name,
         std::optional<int> n_p, std::optional<int> n_m,
         std::string vctrl, double Rm)
        : Component(std::move(name)), n_p_(n_p), n_m_(n_m),
          vctrl_(std::move(vctrl)), Rm_(Rm) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        if (!ctx.vs_index || !ctx.vs_index->count(vctrl_))
            throw std::runtime_error(name_ + ": control source " + vctrl_ + " not found");
        if (!ctx.vs_index->count(name_))
            throw std::runtime_error(name_ + ": CCVS not indexed");
        if (vctrl_ == name_)
            throw std::runtime_error(name_ + ": CCVS cannot control itself");

        int k_ctrl = ctx.vs_index->at(vctrl_);
        int k      = ctx.vs_index->at(name_);

        if (n_p_) { A(n_p_.value(), k) += 1.0; A(k, n_p_.value()) += 1.0; }
        if (n_m_) { A(n_m_.value(), k) -= 1.0; A(k, n_m_.value()) -= 1.0; }
        A(k, k_ctrl) += Rm_;
    }

    std::string type_name() const override { return "CCVS"; }

private:
    std::optional<int> n_p_, n_m_;
    std::string vctrl_;
    double Rm_;
};

// ── CCCS ──────────────────────────────────────────────────────────
class CCCS : public Component {
public:
    CCCS(std::string name,
         std::optional<int> n_p, std::optional<int> n_m,
         std::string vctrl, double gain)
        : Component(std::move(name)), n_p_(n_p), n_m_(n_m),
          vctrl_(std::move(vctrl)), A_(gain) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        if (!ctx.vs_index || !ctx.vs_index->count(vctrl_))
            throw std::runtime_error(name_ + ": controlling source " + vctrl_ + " not found");
        int k_ctrl = ctx.vs_index->at(vctrl_);
        if (n_p_) A(n_p_.value(), k_ctrl) += A_;
        if (n_m_) A(n_m_.value(), k_ctrl) -= A_;
    }

    std::string type_name() const override { return "CCCS"; }

private:
    std::optional<int> n_p_, n_m_;
    std::string vctrl_;
    double A_;
};

} // namespace CircuitEngine
