### Bước 1: Build
```ssh
g++ main.cpp -I . -I D:/ADMIN/eigen-5.0.0 -std=c++17 -O2 -o circuit_engine
```

### Bước 2: RUN
3 mode:
- JSON stdout (cho C# đọc):
```ssh
./circuit_engine my.cir
```
- Bảng DC + ASCII waveform terminal:
```ssh
./circuit_engine my.cir --human          
```
- Vẽ đồ thị bằng HTML:
```ssh
./circuit_engine my.cir --plot              # Chrome/Edge
./circuit_engine my.cir --plot out.html     # ghi ra file cụ thể
```