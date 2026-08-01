import numpy as np
import json

# ===== Raw data =====
freqs = [10000, 50000, 100000, 150000, 200000, 250000, 300000, 350000, 400000, 450000, 500000]
vpp_settings = [50, 70, 90, 110, 130, 150, 170, 190, 210, 230, 250]

adc_data = {
    50:  {10000:3030,50000:3025,100000:3020,150000:3020,200000:3010,250000:3010,300000:3020,350000:3020,400000:3020,450000:3020,500000:3020},
    70:  {10000:3030,50000:3020,100000:3020,150000:3020,200000:3030,250000:3040,300000:3056,350000:3056,400000:3056,450000:3056,500000:3060},
    90:  {10000:3040,50000:3023,100000:3030,150000:3030,200000:3040,250000:3040,300000:3056,350000:3056,400000:3073,450000:3073,500000:3073},
    110: {10000:3030,50000:3023,100000:3023,150000:3030,200000:3030,250000:3040,300000:3056,350000:3056,400000:3073,450000:3073,500000:3080},
    130: {10000:3030,50000:3023,100000:3023,150000:3030,200000:3040,250000:3040,300000:3056,350000:3056,400000:3073,450000:3073,500000:3080},
    150: {10000:3040,50000:3023,100000:3023,150000:3023,200000:3030,250000:3040,300000:3056,350000:3056,400000:3073,450000:3073,500000:3073},
    170: {10000:3030,50000:3030,100000:3030,150000:3023,200000:3040,250000:3040,300000:3056,350000:3056,400000:3073,450000:3080,500000:3073},
    190: {10000:3030,50000:3023,100000:3030,150000:3030,200000:3030,250000:3040,300000:3056,350000:3056,400000:3070,450000:3073,500000:3080},
    210: {10000:3050,50000:3023,100000:3030,150000:3030,200000:3040,250000:3040,300000:3056,350000:3066,400000:3073,450000:3083,500000:3083},
    230: {10000:3040,50000:3030,100000:3030,150000:3040,200000:3040,250000:3045,300000:3050,350000:3056,400000:3073,450000:3083,500000:3090},
    250: {10000:3050,50000:3030,100000:3030,150000:3040,200000:3040,250000:3045,300000:3050,350000:3056,400000:3073,450000:3083,500000:3090},
}

det_data = {
    50:  {10000:566.8,50000:566.9,100000:566.5,150000:566.5,200000:565.4,250000:563.8,300000:561.7,350000:559.3,400000:556.4,450000:553.3,500000:549.7},
    70:  {10000:608.3,50000:608.1,100000:608.0,150000:608.2,200000:607.0,250000:605.2,300000:602.5,350000:599.8,400000:596.1,450000:592.0,500000:587.3},
    90:  {10000:638.7,50000:638.9,100000:638.7,150000:639.0,200000:638.0,250000:636.9,300000:635.0,350000:632.6,400000:629.6,450000:625.9,500000:621.5},
    110: {10000:659.3,50000:659.3,100000:659.2,150000:659.5,200000:658.9,250000:658.0,300000:656.7,350000:654.7,400000:652.3,450000:649.4,500000:645.9},
    130: {10000:674.8,50000:674.9,100000:674.8,150000:675.1,200000:674.7,250000:674.0,300000:672.9,350000:671.3,400000:669.3,450000:666.8,500000:663.8},
    150: {10000:687.7,50000:687.8,100000:687.8,150000:688.2,200000:687.9,250000:687.4,300000:686.4,350000:685.1,400000:683.2,450000:680.1,500000:678.1},
    170: {10000:699.5,50000:699.5,100000:699.6,150000:699.9,200000:699.7,250000:699.3,300000:698.5,350000:697.2,400000:695.4,450000:693.1,500000:690.2},
    190: {10000:710.5,50000:710.5,100000:710.6,150000:711.1,200000:710.9,250000:710.5,300000:709.9,350000:708.6,400000:706.8,450000:704.4,500000:701.5},
    210: {10000:720.6,50000:720.7,100000:720.7,150000:721.3,200000:721.2,250000:721.0,300000:720.4,350000:719.2,400000:717.4,450000:714.9,500000:711.9},
    230: {10000:731.2,50000:731.2,100000:731.3,150000:732.0,200000:731.9,250000:731.7,300000:731.3,350000:730.0,400000:728.2,450000:725.6,500000:722.5},
    250: {10000:741.5,50000:741.6,100000:741.6,150000:742.4,200000:742.3,250000:742.3,300000:742.0,350000:741.0,400000:739.1,450000:736.4,500000:733.0},
}

# Build dataset
data = []
for vpp in vpp_settings:
    for f in freqs:
        vd = det_data[vpp][f]
        vadc = adc_data[vpp][f]
        G = vadc / vpp
        data.append({'Vpp': vpp, 'f': f, 'Vd': vd, 'Vadc': vadc, 'G': G})

print(f'Total data points: {len(data)}')
print(f'Vd range: [{min(d["Vd"] for d in data):.1f}, {max(d["Vd"] for d in data):.1f}] mV')
print(f'G range: [{min(d["G"] for d in data):.2f}, {max(d["G"] for d in data):.2f}]')

# Prepare arrays
X_vd = np.array([d['Vd'] for d in data])
X_f = np.array([d['f'] for d in data])
X_fn = (X_f - 10000) / 490000
y_G = np.array([d['G'] for d in data])

# Compute R2 helper
def r2(y_true, y_pred):
    ss_res = np.sum((y_true - y_pred) ** 2)
    ss_tot = np.sum((y_true - np.mean(y_true)) ** 2)
    return 1 - ss_res / ss_tot

models = {}

# M1: G = a0 + a1*Vd
c = np.polyfit(X_vd, y_G, 1)
Gp = np.polyval(c, X_vd)
e = np.abs((y_G - Gp) / y_G) * 100
models['M1: a0+a1*Vd'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# M2: G = a0 + a1*Vd + a2*Vd^2
c = np.polyfit(X_vd, y_G, 2)
Gp = np.polyval(c, X_vd)
e = np.abs((y_G - Gp) / y_G) * 100
models['M2: a0+a1*Vd+a2*Vd^2'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# M3: G = a0 + a1*Vd + a2*Vd^2 + a3*fn
A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn])
c, _, _, _ = np.linalg.lstsq(A, y_G, rcond=None)
Gp = A @ c
e = np.abs((y_G - Gp) / y_G) * 100
models['M3: a0+a1*Vd+a2*Vd^2+a3*fn'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# M4: G = a0 + a1*Vd + a2*fn
A = np.column_stack([np.ones_like(X_vd), X_vd, X_fn])
c, _, _, _ = np.linalg.lstsq(A, y_G, rcond=None)
Gp = A @ c
e = np.abs((y_G - Gp) / y_G) * 100
models['M4: a0+a1*Vd+a2*fn'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# M5: G = a0 + a1*Vd + a2*Vd^2 + a3*fn + a4*fn*Vd
A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn, X_fn*X_vd])
c, _, _, _ = np.linalg.lstsq(A, y_G, rcond=None)
Gp = A @ c
e = np.abs((y_G - Gp) / y_G) * 100
models['M5: a0+a1*Vd+a2*Vd^2+a3*fn+a4*fn*Vd'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# M6: full 2nd order + cross + fn^2
A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn, X_fn*X_vd, X_fn**2])
c, _, _, _ = np.linalg.lstsq(A, y_G, rcond=None)
Gp = A @ c
e = np.abs((y_G - Gp) / y_G) * 100
models['M6: a0+a1*Vd+a2*Vd^2+a3*fn+a4*fn*Vd+a5*fn^2'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# M7: G = a0 + a1*Vd + a2*fn + a3*fn*Vd
A = np.column_stack([np.ones_like(X_vd), X_vd, X_fn, X_fn*X_vd])
c, _, _, _ = np.linalg.lstsq(A, y_G, rcond=None)
Gp = A @ c
e = np.abs((y_G - Gp) / y_G) * 100
models['M7: a0+a1*Vd+a2*fn+a3*fn*Vd'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# M8: full 2nd order Vd + fn + fn^2 + all cross terms
A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn, X_fn*X_vd, X_fn**2, X_fn**2*X_vd])
c, _, _, _ = np.linalg.lstsq(A, y_G, rcond=None)
Gp = A @ c
e = np.abs((y_G - Gp) / y_G) * 100
models['M8: a0+a1*Vd+a2*Vd^2+a3*fn+a4*fn*Vd+a5*fn^2+a6*fn^2*Vd'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# M9: G = a0 + a1*Vd + a2*Vd^2 + a3*fn + a4*fn^2
A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn, X_fn**2])
c, _, _, _ = np.linalg.lstsq(A, y_G, rcond=None)
Gp = A @ c
e = np.abs((y_G - Gp) / y_G) * 100
models['M9: a0+a1*Vd+a2*Vd^2+a3*fn+a4*fn^2'] = {'coeffs': list(c), 'R2': r2(y_G, Gp), 'MaxE%': float(np.max(e)), 'MAE%': float(np.mean(e))}

# Sort by MAE%
sorted_models = sorted(models.items(), key=lambda x: x[1]['MAE%'])
print('\n=== Model Comparison (sorted by MAE%) ===\n')
for name, m in sorted_models:
    print(f'{name}:')
    print(f'  R2={m["R2"]:.6f}, MAE={m["MAE%"]:.4f}%, MaxE={m["MaxE%"]:.4f}%')
    print(f'  coeffs={m["coeffs"]}')
    print()

# Best model details
best_name, best = sorted_models[0]
print(f'=== BEST: {best_name} ===')

# Now compute Vpp recovery error for the best model
best_coeffs = np.array(best['coeffs'])
print(f'\nCoefficients: {best_coeffs}')

# Reconstruct best model prediction
if 'M1' in best_name:
    G_pred = np.polyval(best_coeffs, X_vd)
elif 'M2' in best_name:
    G_pred = np.polyval(best_coeffs, X_vd)
elif 'M3' in best_name:
    A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn])
    G_pred = A @ best_coeffs
elif 'M4' in best_name:
    A = np.column_stack([np.ones_like(X_vd), X_vd, X_fn])
    G_pred = A @ best_coeffs
elif 'M5' in best_name:
    A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn, X_fn*X_vd])
    G_pred = A @ best_coeffs
elif 'M6' in best_name:
    A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn, X_fn*X_vd, X_fn**2])
    G_pred = A @ best_coeffs
elif 'M7' in best_name:
    A = np.column_stack([np.ones_like(X_vd), X_vd, X_fn, X_fn*X_vd])
    G_pred = A @ best_coeffs
elif 'M8' in best_name:
    A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn, X_fn*X_vd, X_fn**2, X_fn**2*X_vd])
    G_pred = A @ best_coeffs
elif 'M9' in best_name:
    A = np.column_stack([np.ones_like(X_vd), X_vd, X_vd**2, X_fn, X_fn**2])
    G_pred = A @ best_coeffs

# Vpp recovery: Vpp_recovered = Vadc / G_pred
Vadc_all = np.array([d['Vadc'] for d in data])
Vpp_true = np.array([d['Vpp'] for d in data])
Vpp_recovered = Vadc_all / G_pred
Vpp_err_pct = (Vpp_recovered - Vpp_true) / Vpp_true * 100

print(f'\n=== Vpp Recovery Error (Best Model) ===')
print(f'MAE(Vpp): {np.mean(np.abs(Vpp_err_pct)):.4f}%')
print(f'MaxE(Vpp): {np.max(np.abs(Vpp_err_pct)):.4f}%')
print(f'RMSE(Vpp): {np.sqrt(np.mean(Vpp_err_pct**2)):.4f}%')

# Per-frequency breakdown
print(f'\n=== Per-Frequency Vpp Error Breakdown ===')
for f in freqs:
    idx = [i for i, d in enumerate(data) if d['f'] == f]
    err_f = Vpp_err_pct[idx]
    print(f'  {f//1000}kHz: MAE={np.mean(np.abs(err_f)):.4f}%, MaxE={np.max(np.abs(err_f)):.4f}%')

# Per-Vpp breakdown
print(f'\n=== Per-Vpp Error Breakdown ===')
for vpp in vpp_settings:
    idx = [i for i, d in enumerate(data) if d['Vpp'] == vpp]
    err_v = Vpp_err_pct[idx]
    print(f'  {vpp}mVpp: MAE={np.mean(np.abs(err_v)):.4f}%, MaxE={np.max(np.abs(err_v)):.4f}%')

# Save CSV of all data points with predictions
with open('calib_data/total_gain_calib_2026-08-01_v2.csv', 'w') as f:
    f.write('Vpp_set_mV,freq_hz,Vd_mV,Vadc_mV,G_true,G_pred,Vpp_recovered_mV,Vpp_err_pct\n')
    for i, d in enumerate(data):
        f.write(f'{d["Vpp"]},{d["f"]},{d["Vd"]},{d["Vadc"]},{d["G"]:.6f},{G_pred[i]:.6f},{Vpp_recovered[i]:.3f},{Vpp_err_pct[i]:.4f}\n')

print('\nSaved: calib_data/total_gain_calib_2026-08-01_v2.csv')

# Generate C code for the best model
print('\n=== C Code for scope_calib.c ===')
print()
coeffs = best_coeffs
if 'M5' in best_name:
    # a0 + a1*Vd + a2*Vd^2 + a3*fn + a4*fn*Vd
    a0, a1, a2, a3, a4 = coeffs
    print(f'float ScopeAGC_ComputeGain(float vd_mV, float freq_hz)')
    print(f'{{')
    print(f'    /* Vd clamp */')
    print(f'    if (vd_mV < 549.0f) vd_mV = 549.0f;')
    print(f'    if (vd_mV > 743.0f) vd_mV = 743.0f;')
    print(f'    if (freq_hz < 10000.0f) freq_hz = 10000.0f;')
    print(f'    if (freq_hz > 500000.0f) freq_hz = 500000.0f;')
    print(f'')
    print(f'    float fn = (freq_hz - 10000.0f) * (1.0f / 490000.0f);')
    print(f'')
    print(f'    return {a0:.8f}f')
    print(f'         + vd_mV * ({a1:.8f}f + {a2:.8f}f * vd_mV)')
    print(f'         + fn * ({a3:.8f}f + {a4:.8f}f * vd_mV);')
    print(f'}}')
elif 'M6' in best_name:
    a0, a1, a2, a3, a4, a5 = coeffs
    print(f'float ScopeAGC_ComputeGain(float vd_mV, float freq_hz)')
    print(f'{{')
    print(f'    if (vd_mV < 549.0f) vd_mV = 549.0f;')
    print(f'    if (vd_mV > 743.0f) vd_mV = 743.0f;')
    print(f'    if (freq_hz < 10000.0f) freq_hz = 10000.0f;')
    print(f'    if (freq_hz > 500000.0f) freq_hz = 500000.0f;')
    print(f'')
    print(f'    float fn = (freq_hz - 10000.0f) * (1.0f / 490000.0f);')
    print(f'')
    print(f'    return {a0:.8f}f')
    print(f'         + vd_mV * ({a1:.8f}f + {a2:.8f}f * vd_mV)')
    print(f'         + fn * ({a3:.8f}f + {a4:.8f}f * vd_mV + {a5:.8f}f * fn);')
    print(f'}}')
