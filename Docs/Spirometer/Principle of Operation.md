

A [[Spirometer]] works by measuring the **airflow** generated during breathing and converting it into **lung volume and flow parameters**.

## Fundamental Measurement

The fundamental measurement is **airflow (Q)**, from which **lung volume (V)** is calculated by numerical integration:

$$
V(t) = \int Q(t)\, dt
$$

## Signal Processing Chain

1. **Flow sensing** - Flow sensor placed in the breathing path
2. **Signal conditioning** - Amplification and filtering of electrical output
3. **Digitization** - ADC conversion for processing
4. **Software algorithms** - Extract spirometric parameters (FEV₁, FVC, PEF, etc.)

---

*Tags: #spirometer-project #reference #technical*