# Thiết kế metadata thời gian thực và quản lý vòng đời tiến trình

## 1. Mục tiêu và phạm vi

Bổ sung metadata thời gian thực vào cấu trúc tiến trình của xv6,
phục vụ đăng ký tác vụ và kiểm tra khả năng tiếp nhận tải.

Phạm vi triển khai Sprint 3 gồm cấu trúc metadata, cơ chế đồng bộ
và quản lý thông tin đăng ký trong quá trình tạo, kết thúc và thu hồi
tiến trình. Metadata được sử dụng bởi syscall `sched_setdeadline()`
và bộ kiểm tra admission.

Mô hình tác vụ sử dụng `D = T`, các tham số có đơn vị tick.
Cấu hình nghiệm thu chức năng RT sử dụng một CPU (`CPUS=1`).

Scheduler EDF/RM, quản lý ngân sách CPU và vòng đời job được triển khai
ở các sprint tiếp theo.

## 2. Cấu trúc metadata

### 2.1. Định nghĩa

Vị trí: `kernel/proc.h`, sau `enum procstate` và trước `struct proc`.

```c
// All RT fields are protected by rt_lock.
// period = T, runtime = C; D = T.
struct rt_meta {
  int enabled;  // 0: chưa đăng ký RT; 1: đã đăng ký RT.
  int period;   // Chu kỳ T, đơn vị tick.
  int runtime;  // Ngân sách CPU C, đơn vị tick.
};
```

| Trường | Ý nghĩa |
|---|---|
| `enabled` | Xác định tiến trình có đăng ký RT hay không |
| `period` | Chu kỳ T của tác vụ |
| `runtime` | Ngân sách CPU C trong mỗi chu kỳ |

Khi chưa đăng ký, cả ba trường bằng 0. Giá trị `period = 0`
và `runtime = 0` trong trường hợp này là hợp lệ vì `enabled = 0`.

Khi đăng ký thành công, tham số phải thỏa mãn:

```text
0 < runtime <= period
```

Ví dụ:

```c
sched_setdeadline(5, 2);
```

Lời gọi yêu cầu chu kỳ `T = 5` tick và ngân sách CPU `C = 2` tick.
Thứ tự đối số là chu kỳ trước, ngân sách sau.

Trong Sprint 3, việc lưu `runtime` chưa đồng nghĩa kernel đã giới hạn
thời gian chạy của tiến trình theo ngân sách đó.

### 2.2. Tích hợp vào cấu trúc tiến trình

Trường `rt` được bổ sung vào cuối `struct proc`:

```c
  char name[16]; // Tên tiến trình.

  // Protected by rt_lock, not p->lock.
  struct rt_meta rt;
```

Metadata nằm trực tiếp trong `struct proc`, không sử dụng cấp phát
động và không gọi `kfree()` cho `p->rt`.

Cấu trúc `context` và `trapframe` được giữ nguyên để bảo đảm
tính tương thích với mã assembly của xv6.

### 2.3. Khai báo dùng chung

Sau định nghĩa `struct proc`, bổ sung:

```c
// Admission scans proc[i].rt under rt_lock only.
// Never acquire a proc lock or sleep while holding rt_lock.
extern struct spinlock rt_lock;
extern struct proc proc[NPROC];
```

Các khai báo cho phép bộ xử lý syscall truy cập bảng tiến trình
và khóa bảo vệ metadata. Từ khóa `extern` không tạo thêm bảng
tiến trình hoặc cấp phát thêm khóa.

## 3. Trạng thái tiến trình và trạng thái đăng ký RT

Trạng thái tiến trình và trạng thái đăng ký RT là hai thông tin độc lập.

| Thông tin | Trường quản lý | Ví dụ |
|---|---|---|
| Trạng thái tiến trình | `p->state` | RUNNABLE, RUNNING, SLEEPING, ZOMBIE |
| Trạng thái đăng ký RT | `p->rt.enabled` | 0 hoặc 1 |

Một tiến trình có thể ở trạng thái `SLEEPING` nhưng vẫn có
`enabled = 1`. Khi ngủ chờ pipe, tiến trình vẫn giữ đăng ký
và vẫn được tính vào tổng tải.

Không bổ sung các trạng thái job vào `enum procstate`.
Vòng đời job được quản lý riêng khi triển khai scheduler thời gian thực.

## 4. Cơ chế đồng bộ và quy tắc khóa

### 4.1. Phạm vi bảo vệ

| Khóa | Dữ liệu được bảo vệ |
|---|---|
| `p->lock` | Trạng thái và các trường tiến trình theo quy tắc xv6 gốc |
| `rt_lock` | Mọi thao tác đọc, ghi metadata RT và giao dịch admission |

Việc metadata nằm trong `struct proc` không có nghĩa `p->lock`
được sử dụng thay cho `rt_lock`.

Bộ admission giữ `rt_lock` trong toàn bộ quá trình kiểm tra tải
và cập nhật đăng ký. Cách tổ chức này tránh trường hợp hai yêu cầu
cùng kiểm tra một trạng thái tải cũ rồi đồng thời được chấp nhận.

### 4.2. Thứ tự lấy khóa

Nếu cần giữ đồng thời hai khóa, thứ tự là:

```text
Lấy p->lock
    → Lấy rt_lock
    → Xử lý metadata
    → Nhả rt_lock
    → Nhả p->lock khi hoàn tất công việc được bảo vệ
```

Các quy tắc áp dụng:

- Không lấy `p->lock` khi đang giữ `rt_lock`.
- Không gọi `rt_reset()` khi đang giữ `rt_lock`.
- Không gọi `sleep()`, `yield()` hoặc `sched()` khi giữ `rt_lock`.
- Không giữ `rt_lock` qua `swtch()`.
- Giữ nguyên các quy tắc khóa của xv6 gốc.

Bộ admission chỉ lấy `rt_lock`, không lấy thêm khóa tiến trình
trong khi quét metadata.

### 4.3. Khởi tạo khóa

Vị trí: `kernel/proc.c`.

```c
struct proc proc[NPROC];
struct spinlock rt_lock;
```

Trong `procinit()`, khóa được khởi tạo trước vòng lặp khởi tạo
bảng tiến trình:

```c
initlock(&pid_lock, "nextpid");
initlock(&wait_lock, "wait_lock");
initlock(&rt_lock, "rt_lock");
```

`rt_lock` được khởi tạo đúng một lần trước khi cấp phát tiến trình.

## 5. Hàm reset metadata

Vị trí: `kernel/proc.c`.

```c
// Caller must NOT hold rt_lock; may hold p->lock.
static void
rt_reset(struct proc *p)
{
  acquire(&rt_lock);               // Lấy khóa bảo vệ metadata.
  memset(&p->rt, 0, sizeof(p->rt)); // Xóa riêng vùng metadata RT.
  release(&rt_lock);               // Nhả khóa.
}
```

Hàm đưa `enabled`, `period` và `runtime` về 0.
Sau reset, tiến trình không còn đăng ký RT.

Hàm chỉ xóa vùng `p->rt`, không xóa toàn bộ `struct proc`,
do cấu trúc tiến trình còn chứa khóa, context, bảng trang
và các tài nguyên đang sử dụng.

Caller có thể đang giữ `p->lock`, nhưng không được đang giữ
`rt_lock`, vì hàm tự lấy và nhả khóa này.

## 6. Quản lý metadata theo vòng đời tiến trình

| Vị trí | Thao tác | Mục đích |
|---|---|---|
| `procinit()` | Khởi tạo `rt_lock` | Chuẩn bị khóa trước khi sử dụng |
| `allocproc()` | Reset metadata | Tiến trình mới không mang đăng ký cũ |
| `kfork()` | Không sao chép metadata của cha | Con bắt đầu chưa đăng ký RT |
| `kexit()` | Reset trước khi chuyển ZOMBIE | Hủy đăng ký khi tiến trình kết thúc |
| `freeproc()` | Reset trước khi thu hồi tài nguyên | Làm sạch ô tiến trình để tái sử dụng |

### 6.1. Cấp phát tiến trình trong allocproc()

Sau khi tìm được ô `UNUSED`, bổ sung reset ngay sau nhãn `found`:

```c
found:
  rt_reset(p); // Tiến trình mới chưa đăng ký RT.
  p->pid = allocpid();
  p->state = USED;
```

Tại vị trí này, `allocproc()` đã giữ `p->lock`.
Hàm `rt_reset()` chỉ lấy thêm `rt_lock`, tuân thủ thứ tự khóa.

Việc reset ngăn tiến trình mới sử dụng nhầm metadata của tiến trình
trước đó từng chiếm cùng ô trong bảng `proc[]`.

### 6.2. Tạo tiến trình con trong kfork()

Tiến trình con được tạo thông qua `allocproc()`, nên metadata
đã được reset trước khi sao chép bộ nhớ user và các tài nguyên khác.

Không sao chép `p->rt` của cha sang `np->rt` của con.

```c
// Child remains non-RT; do not copy p->rt to np->rt.
```

Nếu cần đăng ký RT, tiến trình con phải tự gọi
`sched_setdeadline()`. Đăng ký của tiến trình cha không thay đổi.

### 6.3. Hủy đăng ký trong kexit()

Metadata được reset sau khi xử lý thư mục hiện tại
và trước khi lấy `wait_lock`:

```c
  p->cwd = 0;

  rt_reset(p); // Hủy đăng ký RT trước ZOMBIE.

  acquire(&wait_lock);
```

Đăng ký phải được hủy trước khi tiến trình chuyển sang `ZOMBIE`.
Nếu chờ đến khi cha gọi `wait()` mới hủy, tiến trình đã kết thúc
có thể tiếp tục chiếm phần tải đăng ký trong thời gian chờ thu hồi.

Lời gọi `kill` chỉ đánh dấu yêu cầu kết thúc. Đăng ký được hủy
khi tiến trình thực sự thực hiện `kexit()`.

### 6.4. Thu hồi trong freeproc()

Ngay đầu `freeproc()`, bổ sung:

```c
  rt_reset(p); // Làm sạch metadata trước khi tái sử dụng ô.
```

Caller của `freeproc()` đã giữ `p->lock`.
Không lấy lại khóa tiến trình trong hàm reset.

Reset tại cả `kexit()` và `freeproc()` không gây trừ tải hai lần.
Tổng tải không được lưu trong một biến riêng mà được tính
bằng cách quét các đăng ký có `enabled = 1`.

## 7. Giao diện với syscall và admission

### 7.1. API đăng ký

```c
int sched_setdeadline(int period, int runtime);
```

| Giá trị trả về | Ý nghĩa |
|---|---|
| `0` | Đăng ký được chấp nhận |
| `-1` | Tham số không hợp lệ |
| `-2` | Tổng tải dự kiến vượt 1 |

Tham số không hợp lệ khi:

```text
period <= 0
runtime <= 0
runtime > period
```

### 7.2. Tính tải và đăng ký lại

Tổng tải được tính từ các tiến trình có `enabled = 1`:

```text
U = Σ(C_i / T_i)
```

Khi một tiến trình đăng ký lại:

1. Bỏ qua đăng ký cũ của chính tiến trình đó khi tính tải dự kiến.
2. Cộng yêu cầu mới vào tổng tải.
3. Nếu được chấp nhận, thay thế đăng ký cũ.
4. Nếu bị từ chối, giữ nguyên toàn bộ đăng ký cũ.

Mọi thao tác đọc, kiểm tra và cập nhật đăng ký phải nằm trong
cùng một lần giữ `rt_lock`.

Không loại đăng ký khỏi tổng tải chỉ vì tiến trình đang ngủ.

### 7.3. Giới hạn của điều kiện admission

Điều kiện `U <= 1` áp dụng cho mô hình EDF một CPU với các tác vụ
độc lập, có thể bị ngắt, `D = T` và bỏ qua chi phí hệ thống.

Điều kiện này không đủ để kết luận tập tác vụ khả thi với RM.
RM cần áp dụng điều kiện phù hợp, chẳng hạn phân tích thời gian
đáp ứng RTA.

Việc được nhận đăng ký trong Sprint 3 chưa chứng minh kernel
thực tế đáp ứng deadline, vì scheduler RT và cơ chế quản lý
ngân sách chưa được triển khai.

## 8. Định hướng mở rộng quản lý job

Một tiến trình RT có thể thực hiện nhiều job liên tiếp.
Hoàn thành một job không kết thúc tiến trình và không hủy đăng ký RT.

Các quy ước thiết kế dự kiến:

- Job đầu release tại thời điểm đăng ký thành công.
- Deadline tuyệt đối bằng thời điểm release cộng T.
- Release tiếp theo dựa trên mốc release trước cộng T.
- Không lấy thời điểm hoàn thành cộng T làm mốc release tiếp theo.
- `rt_job_done()` báo hoàn thành một job và chờ kỳ tiếp theo.
- `exit()` kết thúc tiến trình và hủy đăng ký RT.

Khi triển khai, metadata cần được mở rộng để lưu thời điểm release,
deadline, lượng CPU đã sử dụng và trạng thái job.

Các nội dung cần thống nhất trước khi tích hợp:

| Nội dung | Vấn đề cần giải quyết |
|---|---|
| Hết ngân sách | Job chưa hoàn thành nhưng đã dùng hết C |
| Deadline miss | Ghi nhận một lần và xử lý theo DROP-LATE |
| Release kỳ mới | Xử lý job cũ trước khi tạo job mới |
| Đăng ký lại | Quy định cập nhật khi job đang hoạt động |
| Exec | Quy định giữ hoặc hủy đăng ký khi thay chương trình |
| Đối chiếu simulator | Thống nhất mốc bắt đầu chung hoặc offset tương ứng |
| Đồng bộ | Thiết kế đường đọc, ghi metadata trong timer và scheduler |

Việc xóa metadata không tự động dừng hoặc bỏ qua phần mã user
còn lại của job. Chính sách DROP-LATE cần phối hợp với giao thức
thực thi của chương trình user.

## 9. Kết quả kiểm tra ban đầu

| Nội dung | Kết quả |
|---|---|
| Biên dịch kernel | Thành công |
| Khởi động QEMU với CPUS=1 | Thành công |
| Thực thi lệnh ls | Hoạt động bình thường |
| Thực thi echo quan-meta-ok | In đúng nội dung và trở lại shell |
| Panic trong phiên kiểm tra | Không ghi nhận |

Kết quả xác nhận kernel có thể biên dịch, khởi động và thực hiện
các lệnh shell cơ bản sau khi bổ sung metadata.

Kiểm thử chức năng đăng ký RT được thực hiện sau khi tích hợp
syscall và chương trình `rttest`. Nội dung kiểm thử gồm tính hợp lệ
của tham số, giới hạn tải và hủy đăng ký khi tiến trình kết thúc.

Các trường hợp đăng ký lại, tiến trình con không kế thừa đăng ký
và giữ nguyên metadata khi yêu cầu bị từ chối cần được kiểm tra
bổ sung để đánh giá đầy đủ hợp đồng API.

## 10. Mã nguồn liên quan

| File | Nội dung |
|---|---|
| `kernel/proc.h` | Định nghĩa rt_meta, bổ sung trường rt vào struct proc và khai báo dùng chung |
| `kernel/proc.c` | Định nghĩa và khởi tạo rt_lock, hàm rt_reset, quản lý metadata theo vòng đời tiến trình |
| `kernel/sysproc.c` | Bộ xử lý syscall đăng ký và kiểm tra admission trong phần tích hợp |
| `user/rttest.c` | Chương trình kiểm thử chức năng đăng ký trong phần tích hợp |