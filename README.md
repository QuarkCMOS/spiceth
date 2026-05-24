## Phần 1. Chạy engine
### Bước 1: Cài Eigen
- Tải [thư viện Eigen](https://libeigen.gitlab.io/) và giải nén.
- Thêm đường dẫn của file vừa giải nén vào ```.vscode/c_cpp_properties.json```
```json
{
    "configurations": [
        {
            "name": "Win32",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/src",
                "D:/ADMIN/eigen-5.0.0"
            ],
```

### Bước 2: Nhập netlist
Trong ```src```, tạo 1 file bất kỳ với đuôi .cir và nhập netlist.

my_netlist.cir
```
* Demo: RC charging + pulse source
V1  vp   GND  PULSE(0 5 0 10n 10n 500u 1m)
R1  vp   n1   1k
C1  n1   GND  1u
R2  n1   n2   2k
C2  n2   GND  500n
.TRAN 5u 3m
.end
```

### Bước 3: Build
Thay ```D:/ADMIN/eigen-5.0.0``` bằng đường dẫn của Eigen bên trên
```ssh
g++ main.cpp -I . -I D:/ADMIN/eigen-5.0.0 -std=c++17 -O2 -o circuit_engine
```

### Bước 4: Run
3 kiểu output:
- JSON stdout (dùng cho C# đọc output, WPF sẽ dùng cái này):
```ssh
./circuit_engine my_netlist.cir
```
- Bảng DC + ASCII waveform terminal (vẽ tạm trên terminal):
```ssh
./circuit_engine my_netlist.cir --human          
```
- Vẽ tạm đồ thị bằng HTML:
```ssh
./circuit_engine my_netlist.cir --plot              # Chrome/Edge
./circuit_engine my_netlist.cir --plot out.html     # ghi ra file cụ thể
```


## Phần 2. Test engine
Để kiểm thử kết quả trả về của engine, dùng <b>Ngspice</b> làm tham chiếu so sánh.

(Yêu cầu đã cài Python)
### Bước 1: Cài Ngspice
- Tải phần mềm [Ngspice](https://sourceforge.net/projects/ngspice/files/ng-spice-rework/46/) và giải nén.
- Thêm đường dẫn của <b>Ngspice</b> vào ```tests/runner/run_ngspice```
```python
# Set this if ngspice is not on PATH 
NGSPICE_BIN = r"E:\ADMIN\AppData\Spice64\bin\ngspice.exe"
```
### Bước 2: Tạo test case
Trong ```tests/netlists``` tạo các file ```.cir``` tương ứng với các test case muốn kiểm tra

### Bước 3: Chạy test
- Nếu chạy lần đầu tiên:
```ssh
    python tests/test_main.py --regen-golden
```
-  Chạy những lần sau (do golden đã được tạo, nếu không có golden thì dùng lệnh trên):
```ssh
    python tests/test_main.py
```
- Chỉ test 1 trường hợp:
```ssh
    python tests/test_main.py --filter rc_dc
```
- Chỉnh sai số cho phép giữa output của engine và tham chiếu của <b>Ngspice</b>:
```ssh
    python tests/test_main.py --tol-rel 5e-3
```