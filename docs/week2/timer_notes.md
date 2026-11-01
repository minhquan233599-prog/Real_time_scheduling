# Phân tích cơ chế Timer Interrupt – OPRTS-35

## 1. Luồng xử lý ngắt (Interrupt Handling Flow)

Trong xv6, kernel xử lý trap dựa trên chế độ hoạt động của CPU:

- `usertrap()` trong `kernel/trap.c`: xử lý trap xảy ra khi CPU đang thực thi chương trình ở User mode.
- `kerneltrap()` trong `kernel/trap.c`: xử lý trap xảy ra khi CPU đang thực thi mã kernel ở Kernel mode.

Cả hai hàm đều gọi `devintr()` để xác định nguyên nhân gây ra ngắt.

Hàm `devintr()` trả về:
- `2`: Timer interrupt (ngắt bộ định thời).
- `1`: Ngắt từ thiết bị khác.
- `0`: Không nhận diện được nguồn ngắt.

Khi xác định được đây là timer interrupt, kernel sẽ thực hiện xử lý thông qua hàm `clockintr()`.

## 2. Cơ chế xử lý Timer Interrupt

Hàm `clockintr()` trong `kernel/trap.c` thực hiện việc cập nhật bộ đếm thời gian hệ thống:

```c
if (cpuid() == 0) {
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}
```

Ý nghĩa các câu lệnh:

- `cpuid() == 0`: Chỉ CPU 0 thực hiện tăng bộ đếm thời gian.
- `acquire(&tickslock)`: Lấy khóa để bảo vệ biến dùng chung `ticks`.
- `ticks++`: Tăng bộ đếm thời gian hệ thống lên 1.
- `wakeup(&ticks)`: Đánh thức các tiến trình đang chờ bộ đếm `ticks` thay đổi.
- `release(&tickslock)`: Giải phóng khóa sau khi cập nhật.

Sau đó, kernel thiết lập thời điểm xảy ra timer interrupt tiếp theo:

```c
w_stimecmp(r_time() + 1000000);
```

Theo chú thích trong mã nguồn, giá trị `1000000` tương ứng với khoảng 0,1 giây. Như vậy, chu kỳ timer được cấu hình xấp xỉ 10 lần mỗi giây.

## 3. Khởi tạo Timer

Hàm `timerinit()` trong `kernel/start.c` chịu trách nhiệm khởi tạo bộ định thời:

```c
w_menvcfg(r_menvcfg() | MENVCFG_STCE);
w_mcounteren(r_mcounteren() | 2);
w_stimecmp(r_time() + 1000000);
```

Các thao tác chính:

- Bật phần mở rộng Sstc thông qua bit `MENVCFG_STCE`, cho phép sử dụng thanh ghi `stimecmp`.
- Cho phép chế độ Supervisor truy cập bộ đếm thời gian và thanh ghi so sánh thời gian.
- Thiết lập thời điểm xảy ra timer interrupt đầu tiên bằng `w_stimecmp()`.

## 4. Vai trò của Timer Interrupt trong lập lịch CPU

Khi xảy ra timer interrupt, kernel có thể yêu cầu tiến trình hiện tại nhường CPU để bộ lập lịch lựa chọn tiến trình khác.

Trong `usertrap()`:

```c
if (which_dev == 2)
  yield();
```

Trong `kerneltrap()`:

```c
if (which_dev == 2 && myproc() != 0)
  yield();
```

Hàm `yield()` được gọi khi nguyên nhân ngắt là timer interrupt (`which_dev == 2`). Điều này cho phép tiến trình đang chạy nhường CPU, tạo điều kiện cho các tiến trình khác được thực thi.

## 5. Truy cập bộ đếm thời gian hệ thống

Trong `kernel/sysproc.c`, hàm `sys_uptime()` được sử dụng để đọc giá trị bộ đếm `ticks`:

```c
acquire(&tickslock);
xticks = ticks;
release(&tickslock);
return xticks;
```

Hàm này lấy khóa `tickslock` trước khi đọc giá trị `ticks`, sau đó giải phóng khóa. Việc sử dụng khóa giúp đảm bảo truy cập biến dùng chung an toàn.

Biến `ticks` và khóa `tickslock` được khai báo trong `kernel/defs.h`.

## 6. Tổng kết luồng hoạt động

Cơ chế xử lý timer interrupt trong hệ điều hành xv6 có thể tóm tắt như sau:

1. Timer interrupt xảy ra.
2. Trap được tiếp nhận bởi `usertrap()` hoặc `kerneltrap()`, tùy thuộc vào chế độ hoạt động của CPU.
3. `devintr()` xác định nguồn ngắt và trả về giá trị `2` nếu đó là timer interrupt.
4. `clockintr()` cập nhật bộ đếm `ticks` trên CPU 0 và đánh thức các tiến trình đang chờ.
5. Kernel thiết lập thời điểm xảy ra timer interrupt tiếp theo thông qua `stimecmp`.
6. `yield()` cho phép tiến trình hiện tại nhường CPU để bộ lập lịch lựa chọn tiến trình khác.

Theo cấu hình trong mã nguồn hiện tại, chu kỳ timer xấp xỉ 0,1 giây, tương đương khoảng 10 lần ngắt mỗi giây.