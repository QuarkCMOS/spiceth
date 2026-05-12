# analysis/plot.py

import numpy as np
import matplotlib.pyplot as plt

# Plot Bode transfer function
def plot_bode(results, vin_idx, vout_idx, title="Bode plot"):
    freq = []
    mag =[]
    phase = []

    for f,x in results:
        Vin = x[vin_idx]
        Vout = x[vout_idx]
        if abs(Vin) < 1e-20:
            continue
        H = Vout/Vin # Transfer function

        freq.append(f)
        mag.append(20 * np.log10(max(abs(H), 1e-20)))
        phase.append(np.angle(H, deg=True))

    # Magnitude plot
    plt.figure()
    plt.semilogx(freq, mag)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Magnitude (dB)")
    plt.title(title + " - Magnitude")
    plt.grid(True, which="both")

    # Phase plot
    plt.figure()
    plt.semilogx(freq, phase)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Phase (deg)")
    plt.title(title + " - Phase")
    plt.grid(True, which="both")

    plt.show()


# Plot Transient response
def plot_tran(results, node_idx=1, node_name=None):
    times = [t for t, _ in results]
    values = [x[node_idx] for _, x in results]

    label = node_name if node_name else f"node {node_idx}"

    plt.figure()
    plt.plot(times, values, label=label)
    plt.xlabel("Time (s)")
    plt.ylabel("Voltage (V)")
    plt.title("Transient Response")
    plt.legend()
    plt.grid()
    plt.show()
