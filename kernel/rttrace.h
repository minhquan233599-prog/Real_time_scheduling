#include "types.h"
#ifndef XV6_RTTRACE_H
#define XV6_RTTRACE_H

// Các file sử dụng header này phải include types.h trước.

// Loại sự kiện được lưu trong trace.
enum rt_trace_type {
  RT_EV_RELEASE = 1, // Tạo một job mới.
  RT_EV_DISPATCH,    // Scheduler cho tiến trình chạy.
  RT_EV_TICK,        // Trừ một đơn vị budget.
  RT_EV_BUDGET0,     // Budget vừa giảm về 0.
  RT_EV_COMPLETE,    // Tiến trình báo hoàn thành job.
  RT_EV_MISS,        // Job bị trễ deadline.
  RT_EV_REFILL,      // Cấp lại budget cho job còn dang dở.
  RT_EV_NORMAL
};

// Lệnh điều khiển bộ ghi trace.
enum rt_trace_command {
  RT_TRACE_STOP = 0,
  RT_TRACE_START,
  RT_TRACE_COUNT,
  RT_TRACE_LOST
};

// Phân biệt đường kernel với workload chạy theo từng chính sách.
enum rt_trace_dispatch {
  RT_TRACE_KERNEL = 0,
  RT_TRACE_EDF = 1,
  RT_TRACE_RM = 2
};

// Một bản ghi phản ánh metadata tại thời điểm xảy ra sự kiện.
struct rt_trace_event {
  uint64 tick;
  uint64 job_id;
  uint64 release;
  uint64 deadline;
  uint64 next_release;
  uint64 misses;

  int seq;
  int type;
  int cpu;
  int pid;

  int period;
  int runtime;
  int remaining;
  int kernel_resume;

  // TICK: budget trước khi trừ.
  // DISPATCH: lớp KERNEL hoặc EDF.
  // Các sự kiện khác: 0.
  int aux;
};

#endif