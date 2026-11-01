# Khảo sát scheduler và struct proc trong xv6

- Người thực hiện: Trịnh Minh Quân – 20233599
- Task: OPRTS-34
- Mục tiêu: hiểu cách xv6 quản lý trạng thái process, chọn process chạy và chuyển context; xác định vị trí dự kiến tích hợp EDF/RM.
- Phạm vi: khảo sát và thiết kế, chưa sửa mã nguồn kernel.
- Commit khảo sát trong repo nhóm : 6505f4b1c1214d5be0aade33b1a76076d99802ef

## 1. Trạng thái process

Vị trí: `kernel/proc.h:79`.

```c
enum procstate {
  UNUSED,    // Ô trong bảng process chưa được sử dụng
  USED,      // Ô đã được cấp phát để khởi tạo process
  SLEEPING,  // Process đang chờ một sự kiện
  RUNNABLE,  // Process sẵn sàng chạy nhưng đang chờ CPU
  RUNNING,   // Process đang chạy trên CPU
  ZOMBIE     // Process đã kết thúc, chờ process cha thu hồi
};
```

**Ý nghĩa:** trạng thái cho biết tình trạng hiện tại của process. Scheduler chỉ chọn process ở trạng thái `RUNNABLE`.

| Trạng thái | Mô tả | Có thể được scheduler chọn? |
|---|---|---|
| `UNUSED` | Ô quản lý process đang trống | Không |
| `USED` | Đã cấp phát, chưa sẵn sàng chạy | Không |
| `SLEEPING` | Đang đợi sự kiện | Không |
| `RUNNABLE` | Đã sẵn sàng, đang chờ CPU | Có |
| `RUNNING` | Đang được CPU thực thi | Không chọn như một process đang chờ |
| `ZOMBIE` | Đã kết thúc, chờ thu hồi | Không |

Phân biệt: `RUNNABLE` là có thể chạy; `RUNNING` là đang chạy.

## 2. Cấu trúc struct context

Vị trí: `kernel/proc.h:2–19`.

```c
struct context {
  uint64 ra;   // Địa chỉ trở về khi tiếp tục thực thi
  uint64 sp;   // Con trỏ stack

  // Các thanh ghi callee-saved cần giữ qua lời gọi hàm
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```

**Ý nghĩa:** `struct context` lưu các thanh ghi cần thiết để tạm dừng và tiếp tục một luồng thực thi trong kernel thông qua `swtch()`.

Cấu trúc này không lưu toàn bộ trạng thái thanh ghi của chương trình người dùng. Phần đó được xử lý bằng `trapframe` khi xảy ra trap.

## 3. Cấu trúc struct cpu

Vị trí: `kernel/proc.h:22–27`.

```c
struct cpu {
  struct proc *proc;       // Process đang chạy trên CPU; bằng 0 nếu không có
  struct context context; // Context của scheduler trên CPU này
  int noff;               // Số mức lồng nhau của push_off()
  int intena;             // Trạng thái interrupt trước lần push_off() ngoài cùng
};
```

**Ý nghĩa:** mỗi CPU có một `struct cpu` để quản lý process đang chạy, context của scheduler và trạng thái kiểm soát interrupt.

`noff` theo dõi độ lồng của thao tác tắt interrupt, không phải một biến đếm tổng quát cho mọi loại lock.

## 4. Cấu trúc struct proc

Vị trí: `kernel/proc.h:82–104`.

```c
struct proc {
  struct spinlock lock;       // Lock bảo vệ các trường dùng chung bên dưới

  // Phải giữ p->lock khi sử dụng các trường này
  enum procstate state;       // Trạng thái process
  void *chan;                 // Kênh sự kiện process đang chờ khi ngủ
  int killed;                 // Cờ báo process đã được yêu cầu kết thúc
  int xstate;                 // Mã thoát trả cho process cha qua wait()
  int pid;                    // Mã định danh process

  // Phải giữ wait_lock khi sử dụng trường này
  struct proc *parent;        // Con trỏ tới process cha

  // Các trường riêng của process, không yêu cầu giữ p->lock
  uint64 kstack;              // Địa chỉ ảo của kernel stack
  uint64 sz;                  // Kích thước bộ nhớ process, tính bằng byte
  pagetable_t pagetable;      // Bảng trang của không gian người dùng
  struct trapframe *trapframe; // Vùng dữ liệu phục vụ xử lý trap
  struct context context;     // Context để tiếp tục chạy process trong kernel
  struct file *ofile[NOFILE]; // Các file đang mở
  struct inode *cwd;          // Thư mục làm việc hiện tại
  char name[16];              // Tên process phục vụ debug
};
```

**Ý nghĩa:** `struct proc` là bản ghi kernel sử dụng để quản lý một process, bao gồm trạng thái, bộ nhớ, file và context.

Các trường liên quan trực tiếp đến khảo sát scheduler:

| Trường | Vai trò |
|---|---|
| `state` | Xác định process có đủ điều kiện chạy không |
| `lock` | Bảo vệ trạng thái và phối hợp với scheduler |
| `context` | Lưu vị trí thực thi để process có thể tiếp tục chạy |
| `pid` | Nhận diện process khi theo dõi và ghi log |
| `chan` | Xác định sự kiện mà process đang chờ |

Thông tin thời gian thực như period, budget và deadline chưa xuất hiện trong cấu trúc này. 

## 5. Hàm scheduler()

Vị trí: `kernel/proc.c:428–470`.

```c
void
scheduler(void)
{
  struct proc *p;                   // Con trỏ dùng để duyệt bảng process
  struct cpu *c = mycpu();          // Lấy thông tin CPU hiện tại

  c->proc = 0;                     // CPU chưa gắn với process đang chạy
  for (;;) {                       // Scheduler lặp liên tục, không trả về
    intr_on();                     // Cho phép xử lý interrupt để tránh deadlock
    intr_off();                    // Tắt lại để tránh race giữa interrupt và wfi

    int found = 0;                 // Chưa tìm thấy process để chạy trong lượt này
    for (p = proc; p < &proc[NPROC]; p++) { // Duyệt từng ô trong bảng process
      acquire(&p->lock);           // Giữ lock trước khi kiểm tra trạng thái
      if (p->state == RUNNABLE) {  // Chỉ chọn process sẵn sàng chạy
        p->state = RUNNING;       // Đánh dấu process đang chạy
        c->proc = p;              // Gắn process được chọn với CPU hiện tại
        swtch(&c->context, &p->context); // Lưu scheduler, chuyển sang process

        mycpu()->intena = 0;       // Không bật lại interrupt khi release lock

        c->proc = 0;              // Process đã chuyển quyền về scheduler
        found = 1;                // Đã chạy ít nhất một process trong lượt duyệt
      }
      release(&p->lock);           // Nhả lock trước khi xét process tiếp theo
    }
    if (found == 0) {              // Cả lượt duyệt không tìm được process để chạy
      asm volatile("wfi");        // Chờ interrupt thay vì tiếp tục quay vòng
    }
  }
}
```

**Ý nghĩa:** `scheduler()` chạy trên mỗi CPU, duyệt bảng process để tìm process `RUNNABLE`, chuyển trạng thái sang `RUNNING` rồi trao CPU bằng `swtch()`. Khi process trả quyền thực thi, scheduler tiếp tục lượt duyệt.

`found = 1` chỉ cho biết đã có process được chạy trong lượt duyệt, không có nghĩa process đó đã kết thúc.

Scheduler gốc lựa chọn theo lượt duyệt bảng process, chưa so sánh deadline hoặc period.

### Quy ước lock khi chuyển context

Scheduler giữ `p->lock` trước khi chuyển sang process. Phía process có trách nhiệm nhả lock sau khi được tiếp tục chạy và giữ lại lock trước khi chuyển về scheduler.

Do đó, lock được phối hợp giữa scheduler và process qua `swtch()`, không chỉ nằm trong một đoạn thực thi liên tục.

## 6. Hàm sched()

Vị trí: `kernel/proc.c:479–497`.

```c
void
sched(void)
{
  int intena;                      // Biến lưu trạng thái phục hồi interrupt
  struct proc *p = myproc();        // Lấy process hiện tại

  if (!holding(&p->lock))           // Phải đang giữ lock của process
    panic("sched p->lock");         // Dừng kernel nếu vi phạm điều kiện
  if (mycpu()->noff != 1)           // Phải có đúng một mức push_off()
    panic("sched locks");          // Phát hiện điều kiện khóa không phù hợp
  if (p->state == RUNNING)          // Phải đổi trạng thái trước khi nhường CPU
    panic("sched RUNNING");        // Không cho chuyển với trạng thái RUNNING
  if (intr_get())                   // Interrupt phải đang bị tắt
    panic("sched interruptible");   // Dừng nếu có thể bị interrupt lúc này

  intena = mycpu()->intena;         // Lưu trạng thái interrupt của luồng hiện tại
  swtch(&p->context, &mycpu()->context); // Lưu process, chuyển về scheduler
  mycpu()->intena = intena;         // Khôi phục giá trị khi process chạy lại
}
```

**Ý nghĩa:** `sched()` kiểm tra các điều kiện cần thiết rồi chuyển context từ process về scheduler. Hàm không chọn process tiếp theo và không tự thay đổi `p->state`; phía gọi phải thiết lập trạng thái trước.

Khi process được chọn chạy lại, lời gọi `swtch()` trở về và hàm khôi phục `intena`. Gán `intena` không trực tiếp bật interrupt mà khôi phục thông tin phục vụ việc quản lý interrupt.

## 7. Hàm yield()

Vị trí: `kernel/proc.c:500–508`.

```c
void
yield(void)
{
  struct proc *p = myproc(); // Lấy process đang chạy trên CPU hiện tại
  acquire(&p->lock);         // Giữ lock để bảo vệ dữ liệu process
  p->state = RUNNABLE;      // Đánh dấu process sẵn sàng được chạy lại
  sched();                  // Chuyển về scheduler; tạm dừng process tại đây
  release(&p->lock);         // Nhả lock khi process được chạy lại
}
```

**Ý nghĩa:** `yield()` cho phép process đang chạy nhường CPU nhưng vẫn sẵn sàng chạy tiếp. Khi scheduler chọn lại process, nó tiếp tục sau lời gọi `sched()` rồi nhả lock.

`yield()` không chuyển process thành `SLEEPING` và không kết thúc process.

## 8. Phân biệt các hàm

| Hàm | Trách nhiệm |
|---|---|
| `scheduler()` | Chọn process sẵn sàng để chạy |
| `yield()` | Đặt process thành `RUNNABLE` rồi nhường CPU |
| `sched()` | Kiểm tra điều kiện và chuyển về scheduler |
| `swtch()` | Lưu context hiện tại và khôi phục context đích |

Hai hướng chuyển context được thể hiện trong mã nguồn:

```c
// kernel/proc.c:453 — scheduler chuyển sang process
swtch(&c->context, &p->context);

// kernel/proc.c:495 — process chuyển về scheduler
swtch(&p->context, &mycpu()->context);
```

**Ý nghĩa:** scheduler và process có context riêng. `swtch()` cho phép tiếp tục thực thi từ context đã lưu, thay vì bắt đầu lại hàm từ đầu.

## 9. Vị trí dự kiến tích hợp EDF/RM

Vị trí cần xem xét là đoạn lựa chọn process trong
`kernel/proc.c:445–453`, trước khi gán `RUNNING` và chuyển context.

Đây là đoạn mã giả thiết kế

```c
if (policy == EDF) {
  p = edf_pick(); // Chọn ứng viên có deadline tuyệt đối gần nhất
} else if (policy == RM) {
  p = rm_pick();  // Chọn ứng viên có period nhỏ nhất
}
```

**Ý nghĩa:** EDF/RM thay đổi chính sách lựa chọn process. Cơ chế chuyển context vẫn sử dụng `swtch()` của xv6.

| Thuật toán | Tiêu chí ưu tiên |
|---|---|
| EDF | Deadline tuyệt đối nhỏ hơn |
| RM | Period nhỏ hơn |

Một process thời gian thực dự kiến chỉ được xét khi:

- Có trạng thái `RUNNABLE`.
- Job hiện tại đã được phát hành.
- Job còn budget.
- Job vẫn được phép chạy theo chính sách xử lý deadline của nhóm.

## 10. Kết quả khảo sát

Qua mã nguồn đã đọc:

- Scheduler chỉ chọn process `RUNNABLE`.
- `struct proc` lưu trạng thái và context của từng process.
- Mỗi CPU có context scheduler riêng trong `struct cpu`.
- `yield()` đổi trạng thái thành `RUNNABLE` rồi gọi `sched()`.
- `sched()` kiểm tra điều kiện và gọi `swtch()` để trở về scheduler.
- `p->lock` bảo vệ trạng thái và phối hợp việc chuyển context.
- EDF/RM dự kiến thay đổi phần lựa chọn process, đồng thời phải giữ đúng các điều kiện khóa và trạng thái của xv6.
