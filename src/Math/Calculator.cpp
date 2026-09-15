#include "Calculator.hpp"
#include "../Data/State.hpp"
#include "../Common.hpp"
#include <thread>
#include <vector>
#include <algorithm>
#include <cmath>

const int ERFC_LUT_SIZE = 100000;
const double ERFC_LUT_MAX = 6.0;
static double g_erfcLUT[ERFC_LUT_SIZE + 1];
static bool g_erfcLUT_inited = false;

// 初始化 erfc() 快速查找表
void initErfcLUT() {
    if (g_erfcLUT_inited) return;
    for (int i = 0; i <= ERFC_LUT_SIZE; ++i) {
        double x = (double)i / ERFC_LUT_SIZE * ERFC_LUT_MAX;
        g_erfcLUT[i] = std::erfc(x);
    }
    g_erfcLUT_inited = true;
}

// 基于一阶线性插值的快速 erfc() 计算
double fast_erfc(double x) {
    if (std::isnan(x)) return 1.0;
    if (x >= ERFC_LUT_MAX) return 0.0;
    if (x <= 0.0) return 1.0;
    double scaled = x * (ERFC_LUT_SIZE / ERFC_LUT_MAX);
    int idx = static_cast<int>(scaled);
    if (idx >= ERFC_LUT_SIZE) return 0.0;
    double frac = scaled - idx;
    return g_erfcLUT[idx] + frac * (g_erfcLUT[idx + 1] - g_erfcLUT[idx]);
}

void stopGlobalRecalc() {
    g_calcId++;
    g_isCalculating = false;
    g_forcePrecRedraw = true;
}

void startGlobalRecalc() {
    g_calcId++;
    int currentId = g_calcId.load();

    g_isCalculating = true;
    g_forcePrecRedraw = true;
    g_calcProgress.store(0.0f);

    initErfcLUT();
    updateTickCache();

    // 筛选已激活，应该参与计算的操作
    std::vector<FrameAction> validActions;
    validActions.reserve(g_tickActionsCache.size());
    for (const auto& act : g_tickActionsCache) {
        if (act.shouldDraw) validActions.push_back(act);
    }

    if (validActions.empty()) {
        g_validActions.clear();
        g_isCalculating = false;
        g_forcePrecRedraw = true;
        return;
    }

    double fps = g_macroFps > 0.0 ? g_macroFps : 240.0;
    double respawnTime = g_respawnTime;
    double targetTime = g_targetTime > 0.0 ? g_targetTime : DEFAULT_TARGET_TIME;
    double kT = g_kT;
    double kU = g_kU;
    double kC = g_kC;

    std::thread([currentId, validActions, fps, respawnTime, targetTime, kT, kU, kC]() mutable {
        size_t N = validActions.size();

        // ----------------- 1. 将swift click解包 -----------------
        struct FlatInput {
            double time;
            int input;
            double window;
        };

        std::vector<FlatInput> flatInputs;
        std::vector<int> lastFlatIdx(N, 0);
        int currentInputNumber = 1;

        for (size_t i = 0; i < N; ++i) {
            double t_i = respawnTime + (validActions[i].frame / fps);
            double fw = validActions[i].frameWindow <= 0.0 ? 1.0 : validActions[i].frameWindow;
            int k = validActions[i].ifCount > 0 ? validActions[i].ifCount : 1;

            if (k <= 1) {
                flatInputs.push_back({ t_i, currentInputNumber++, fw });
            }
            else {
                double w_main = std::max(fw - (k - 1.0) / k, 1.0 / k);
                double w_sub = 1.0 / k;

                // 主输入
                flatInputs.push_back({ t_i, currentInputNumber++, w_main });
                // 次级微输入
                for (int s = 0; s < k - 1; ++s) {
                    flatInputs.push_back({ t_i, currentInputNumber++, w_sub });
                }
            }
            // 记录动作 i 在解包后对应的最末一个微输入下标
            lastFlatIdx[i] = static_cast<int>(flatInputs.size() - 1);
        }

        size_t M = flatInputs.size();

        // ----------------- 2. 逐输入计算权重 -----------------
        std::vector<double> vBase(N, 0.0), vN(N, 0.0), vF(N, 0.0), vC(N, 0.0);
        std::vector<double> vNF(N, 0.0), vNC(N, 0.0), vFC(N, 0.0), vNFC(N, 0.0);

        std::vector<double> W_Base(M, 0.0), W_N(M, 0.0), W_F(M, 0.0), W_C(M, 0.0);
        std::vector<double> W_NF(M, 0.0), W_NC(M, 0.0), W_FC(M, 0.0), W_NFC(M, 0.0);
        std::vector<double> T(M, 0.0);

        const double MAGIC_MULT = 0.5 * 0.7071067811865475;
        double prev_time = 0.0;
        int prev_input = 0;

        for (size_t m = 0; m < M; ++m) {
            double t_m = flatInputs[m].time;
            int inp = flatInputs[m].input;
            T[m] = t_m;

            double w_m = flatInputs[m].window / fps;

            double n_mult = std::exp(-kT * t_m);
            double f_mult = std::exp(-kU * inp);

            double deltaTime = 1.0;
            if (inp - prev_input != 0) {
                double dt = (t_m - prev_time) / static_cast<double>(inp - prev_input);
                deltaTime = (dt == 0.0) ? 1.0 : dt;
            }

            double max_val = std::max(1.0, 2.0 / deltaTime);
            double c_mult = std::pow(4.0 / max_val, kC);

            double base_w = w_m * MAGIC_MULT;
            W_Base[m] = base_w;
            W_N[m] = base_w * n_mult;
            W_F[m] = base_w * f_mult;
            W_NF[m] = base_w * n_mult * f_mult;
            W_C[m] = base_w * c_mult;
            W_NC[m] = base_w * n_mult * c_mult;
            W_FC[m] = base_w * f_mult * c_mult;
            W_NFC[m] = base_w * n_mult * f_mult * c_mult;

            prev_time = t_m;
            prev_input = inp;
        }

        // ----------------- 3. 给定精度 L，计算期望通关总时间 E[Tc] -----------------
        auto evalTc = [&](int maxIndex, int start_idx, const std::vector<double>& W_prime, double L) {
            if (std::isnan(L) || L <= 0.0) return 1e100;

            double r = 1.0;
            double E_Ta_fails = 0.0;
            double t_n = T[maxIndex];

            for (int j = start_idx; j <= maxIndex; ++j) {
                double x = W_prime[j] * L;
                if (x > 6.0) continue;
                double q_i = fast_erfc(x);
                double p_i = 1.0 - q_i;
                if (p_i < 1e-15) p_i = 1e-15;

                E_Ta_fails += T[j] * r * q_i;
                r *= p_i;
                if (r < 1e-200) { r = 0.0; break; }
            }
            if (r <= 0.0 || std::isnan(r) || std::isnan(E_Ta_fails)) return 1e100;
            double E_Ta = t_n * r + E_Ta_fails;
            double res = E_Ta / r;
            return (std::isnan(res) || std::isinf(res)) ? 1e100 : res;
            };

        // ----------------- 4. 求解单点的 L* 值 -----------------
        auto calcFast = [&](int maxIndex, int& start_idx, const std::vector<double>& W_prime, double prev_L) {
            if (std::isnan(prev_L) || prev_L < MINLEFT) prev_L = MINLEFT;

            while (start_idx < maxIndex && W_prime[start_idx] * prev_L > 6.5) {
                start_idx++;
            }

            double L0 = prev_L;
            double f0 = evalTc(maxIndex, start_idx, W_prime, L0) - targetTime;
            if (f0 <= 0.0 && f0 > -1.0) return L0;

            double L1 = L0 * 1.001 + 0.001;
            double f1 = evalTc(maxIndex, start_idx, W_prime, L1) - targetTime;
            double L_next = L1;
            bool converged = false;

            // 使用割线法求解
            for (int iter = 0; iter < MAXITERATION1; ++iter) {
                double denom = f1 - f0;
                if (std::abs(denom) < 1e-9 || std::isnan(denom) || std::isinf(denom)) break;

                double delta = f1 * (L1 - L0) / denom;
                if (std::isnan(delta) || std::isinf(delta)) break;

                L_next = L1 - delta;
                if (std::isnan(L_next) || std::isinf(L_next)) break;

                if (L_next < MINLEFT) L_next = MINLEFT;
                if (L_next > MAXRIGHT) L_next = MAXRIGHT;

                double f_next = evalTc(maxIndex, start_idx, W_prime, L_next) - targetTime;
                if (std::isnan(f_next)) break;

                if (std::abs(f_next) < 1e-4) {
                    converged = true;
                    break;
                }
                L0 = L1; f0 = f1;
                L1 = L_next; f1 = f_next;
            }

            // 若割线法不收敛，则使用二分法
            if (!converged) {
                double left = prev_L;
                if (std::isnan(left) || left < MINLEFT) left = MINLEFT;

                double right = prev_L + 10.0;
                int expCount = 0;
                while (evalTc(maxIndex, start_idx, W_prime, right) > targetTime && expCount < 64) {
                    right *= 2.0;
                    expCount++;
                    if (right > MAXRIGHT) { right = MAXRIGHT; break; }
                }
                for (int iter = 0; iter < MAXITERATION2; ++iter) {
                    double mid = (left + right) * 0.5;
                    double val = evalTc(maxIndex, start_idx, W_prime, mid);
                    if (val > targetTime) left = mid; else right = mid;
                    if ((right - left < 1e-5) || ((right - left) / (mid > 0 ? mid : 1.0) < 1e-5)) break;
                }
                L_next = (left + right) * 0.5;
            }

            if (std::isnan(L_next) || std::isinf(L_next)) return MINLEFT;
            return std::clamp(L_next, static_cast<double>(MINLEFT), static_cast<double>(MAXRIGHT));
            };

        std::atomic<int> completedTasks{ 0 };
        const int TOTAL_TASKS = static_cast<int>(N * 8);

        auto runMetric = [&](const std::vector<double>& W, std::vector<double>& vOut) {
            double prev_v = MINLEFT;
            int start_idx = 0;
            for (size_t i = 0; i < N; ++i) {
                if (g_calcId.load() != currentId) return;
                bool is_last_in_frame = (i == N - 1) || (validActions[i].frame != validActions[i + 1].frame);

                if (is_last_in_frame) {
                    // 解包模式下，传入动作 i 包含的所有微输入的最后下标
                    int flat_max = lastFlatIdx[i];
                    vOut[i] = calcFast(flat_max, start_idx, W, prev_v);
                    prev_v = vOut[i];
                }
                else {
                    vOut[i] = prev_v;
                }

                int completed = ++completedTasks;
                if (completed % 100 == 0 || completed == TOTAL_TASKS) {
                    g_calcProgress.store(static_cast<float>(completed) / TOTAL_TASKS * 100.0f, std::memory_order_relaxed);
                }
            }
            };

        // 8 线程运算
        std::thread t1([&]() { runMetric(W_Base, vBase); });
        std::thread t2([&]() { runMetric(W_N, vN); });
        std::thread t3([&]() { runMetric(W_F, vF); });
        std::thread t4([&]() { runMetric(W_C, vC); });
        std::thread t5([&]() { runMetric(W_NF, vNF); });
        std::thread t6([&]() { runMetric(W_NC, vNC); });
        std::thread t7([&]() { runMetric(W_FC, vFC); });
        std::thread t8([&]() { runMetric(W_NFC, vNFC); });

        t1.join(); t2.join(); t3.join(); t4.join();
        t5.join(); t6.join(); t7.join(); t8.join();

        if (g_calcId.load() != currentId) return;

        geode::Loader::get()->queueInMainThread([currentId, validActions, vBase, vN, vF, vC, vNF, vNC, vFC, vNFC]() mutable {
            if (g_calcId.load() == currentId) {
                g_validActions = std::move(validActions);
                g_vBase = std::move(vBase);
                g_vN = std::move(vN);
                g_vF = std::move(vF);
                g_vC = std::move(vC);
                g_vNF = std::move(vNF);
                g_vNC = std::move(vNC);
                g_vFC = std::move(vFC);
                g_vNFC = std::move(vNFC);

                g_isCalculating = false;
                g_forcePrecRedraw = true;
                g_isLStarDirty = false;
            }
            });
        }).detach();
}