// SPDX-License-Identifier: GPL-3.0-or-later
#include "collector.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "bandsight.skel.h"

namespace bandsight {
namespace {

int quiet_libbpf_print(enum libbpf_print_level level, const char* fmt, va_list args) {
    if (level == LIBBPF_DEBUG) return 0;
    return std::vfprintf(stderr, fmt, args);
}

// Mirrors struct bs_proc_event in bpf/bandsight.bpf.c field for field: type,
// tgid, then the fixed exe buffer. Order, widths and array length must match
// exactly, or the ring buffer's bytes get silently reinterpreted.
struct BsProcEventRaw {
    std::uint32_t type;
    std::uint32_t tgid;
    char exe[256];
};

int on_ring_event(void* ctx, void* data, size_t size) {
    if (size < sizeof(BsProcEventRaw)) return 0;
    auto* pending = static_cast<std::vector<ProcEvent>*>(ctx);
    const auto* raw = static_cast<const BsProcEventRaw*>(data);
    ProcEvent ev;
    ev.type = (raw->type == 1) ? ProcEvent::Type::Exec : ProcEvent::Type::Exit;
    ev.tgid = raw->tgid;
    // bpf_probe_read_kernel_str NUL-terminates on the kernel side, but the
    // buffer is still a fixed array, not a guaranteed C string here - bound
    // the read with strnlen rather than trusting termination.
    ev.exe = std::string(raw->exe, ::strnlen(raw->exe, sizeof(raw->exe)));
    pending->push_back(std::move(ev));
    return 0;
}

}  // namespace

struct Collector::Impl {
    struct bandsight_bpf* skel = nullptr;
    struct ring_buffer* rb = nullptr;
    std::vector<ProcEvent> pending;

    // events_dropped rate-limit state (FOLLOWUP I3 part b): drop_total is the
    // map value already folded into drops_since_log, whether or not that
    // count has been logged yet - so no read of the map is ever double
    // counted, only its logging is delayed.
    // Links we attached one program at a time, so they can be freed even though
    // the skeleton never learned about them (see load(), I2).
    std::vector<struct bpf_link*> links;

    std::uint64_t drop_total = 0;
    std::uint64_t drops_since_log = 0;
    bool drop_log_started = false;
    std::chrono::steady_clock::time_point last_drop_log{};
};

Collector::Collector() : impl_(new Impl) {}

Collector::~Collector() {
    if (impl_) {
        // The ring buffer holds a reference to a map fd owned by the skeleton,
        // so it must be torn down first.
        if (impl_->rb) ring_buffer__free(impl_->rb);
        // Destroyed before the skeleton, and by us: these links were created by
        // bpf_program__attach rather than by the skeleton's own attach, so
        // bandsight_bpf__destroy does not know about them.
        for (struct bpf_link* link : impl_->links) bpf_link__destroy(link);
        impl_->links.clear();
        if (impl_->skel) bandsight_bpf__destroy(impl_->skel);
        delete impl_;
    }
}

bool Collector::load() {
    libbpf_set_print(quiet_libbpf_print);

    impl_->skel = bandsight_bpf__open();
    if (!impl_->skel) {
        // The load and attach paths below name their errno, and a bare "open
        // failed" here was the one remaining way to lose the actual cause -
        // most usefully EPERM, which reads as a broken build rather than as
        // missing capabilities. bandsight_bpf__open() returns NULL, not an
        // errno, so this is the one place errno is all there is.
        const int err = errno;
        if (err == EPERM || err == EACCES) {
            std::fprintf(stderr,
                         "bandsightd: BPF open failed: %s; the daemon needs "
                         "CAP_BPF and CAP_PERFMON - run as root or via the "
                         "systemd unit; interface totals only\n",
                         std::strerror(err));
        } else {
            std::fprintf(stderr,
                         "bandsightd: BPF open failed: %s; interface totals only\n",
                         std::strerror(err));
        }
        return false;
    }
    if (const int rc = bandsight_bpf__load(impl_->skel); rc != 0) {
        // bandsight_bpf__load() returns bpf_object__load_skeleton()'s result
        // directly: 0 on success, negative errno otherwise, with errno also
        // set to the positive value (see libbpf.h). Prefer the return value;
        // it can't have been clobbered by anything running in between.
        const int err = (rc < 0) ? -rc : errno;
        if (err == EPERM || err == EACCES) {
            std::fprintf(stderr,
                         "bandsightd: BPF load failed: %s; the daemon needs "
                         "CAP_BPF and CAP_PERFMON - run as root or via the "
                         "systemd unit; interface totals only\n",
                         std::strerror(err));
        } else {
            std::fprintf(stderr,
                         "bandsightd: BPF load failed: %s; interface totals only\n",
                         std::strerror(err));
        }
        bandsight_bpf__destroy(impl_->skel);
        impl_->skel = nullptr;
        return false;
    }
    // Attached one program at a time rather than through bandsight_bpf__attach,
    // which fails as a unit (I2). The spec's failure-mode requirement is to log
    // the SPECIFIC failing attach point and continue - and a single renamed
    // kernel symbol otherwise costs all per-app accounting, including the four
    // hooks that would still have attached, with no way to tell which broke.
    int attached = 0, failed = 0;
    struct bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, impl_->skel->obj) {
        struct bpf_link* link = bpf_program__attach(prog);
        if (!link) {
            const int err = errno;
            ++failed;
            std::fprintf(stderr, "bandsightd: BPF attach failed for %s: %s\n",
                         bpf_program__name(prog), std::strerror(err));
            if (err == EPERM || err == EACCES) {
                std::fprintf(stderr,
                             "bandsightd: the daemon needs CAP_BPF and CAP_PERFMON "
                             "- run as root or via the systemd unit\n");
            }
            continue;
        }
        impl_->links.push_back(link);
        ++attached;
    }

    if (attached == 0) {
        std::fprintf(stderr,
                     "bandsightd: no BPF program attached; interface totals only\n");
        for (struct bpf_link* link : impl_->links) bpf_link__destroy(link);
        impl_->links.clear();
        bandsight_bpf__destroy(impl_->skel);
        impl_->skel = nullptr;
        return false;
    }

    degraded_ = false;
    if (failed > 0) {
        // Partial accounting is worth having and worth saying out loud: the
        // per-application numbers are real but incomplete, and silently serving
        // them as if they were whole is the failure this message exists to
        // prevent.
        std::fprintf(stderr,
                     "bandsightd: BPF attached %d of %d programs; per-app "
                     "accounting is ACTIVE BUT INCOMPLETE - traffic through the "
                     "failed hooks above is not counted\n",
                     attached, attached + failed);
    } else {
        std::fprintf(stderr, "bandsightd: BPF attached, per-app accounting active\n");
    }

    impl_->rb = ring_buffer__new(bpf_map__fd(impl_->skel->maps.events), on_ring_event,
                                 &impl_->pending, nullptr);
    if (!impl_->rb) {
        std::fprintf(stderr,
                     "bandsightd: ringbuf setup failed; identity falls back to "
                     "/proc only\n");
    }

    return true;
}

std::vector<std::pair<AppKey, AppVal>> Collector::drain_app() {
    std::vector<std::pair<AppKey, AppVal>> out;
    if (degraded_ || !impl_->skel) return out;

    const int fd = bpf_map__fd(impl_->skel->maps.map_app);
    if (fd < 0) return out;

    constexpr __u32 kBatch = 512;
    AppKey keys[kBatch];
    AppVal vals[kBatch];

    // The batch cursor is a bucket index, not a pointer. For BPF_MAP_TYPE_HASH the
    // kernel reads and writes a __u32 through these arguments, so the storage must
    // be a __u32 - a void* "happens to work" only because it is oversized, and it
    // misrepresents what the value is.
    __u32 cursor = 0;
    bool first_call = true;

    for (;;) {
        __u32 count = kBatch;

        // LOOKUP_AND_DELETE_BATCH is atomic per entry: increments landing during
        // the drain are either returned now or retained for the next drain, never
        // dropped. This is the difference from read-then-zero.
        //
        // in_batch MUST be a literal NULL on the first call - that is how libbpf
        // is told to start at the beginning of the map. Passing a pointer to zeroed
        // storage instead relies on the hash-map implementation treating a zero
        // cursor as "no cursor", which is an internal detail and not the contract.
        errno = 0;
        const int rc = bpf_map_lookup_and_delete_batch(
            fd, first_call ? nullptr : &cursor, &cursor, keys, vals, &count, nullptr);
        first_call = false;

        // Entries are consumed before inspecting rc: the terminating call still
        // returns the final partial batch alongside its non-zero status.
        for (__u32 i = 0; i < count; ++i) {
            out.emplace_back(keys[i], vals[i]);
        }

        if (rc != 0) {
            // ENOENT is the normal end-of-map signal. Anything else is a real
            // fault, and on EFAULT the kernel may have deleted entries without
            // returning them - that is silent data loss, so it must be logged
            // rather than mistaken for a clean finish.
            const int err = (rc < 0 && rc != -1) ? -rc : errno;
            if (err != ENOENT) {
                std::fprintf(stderr,
                             "bandsightd: map drain failed (%s); some byte counts "
                             "for this interval were lost\n",
                             std::strerror(err));
            }
            break;
        }

        // A zero-length success would otherwise spin forever.
        if (count == 0) break;
    }

    return out;
}

void Collector::check_dropped_events() {
    if (!impl_->skel) return;

    const int fd = bpf_map__fd(impl_->skel->maps.events_dropped);
    if (fd < 0) return;

    const __u32 key = 0;
    __u64 total = 0;
    if (bpf_map_lookup_elem(fd, &key, &total) != 0) return;
    if (total <= impl_->drop_total) return;  // nothing new since the last read

    impl_->drops_since_log += total - impl_->drop_total;
    impl_->drop_total = total;

    // Same rate-limit shape as DaemonLoop::log_tick_error: log immediately the
    // first time, then at most once a minute, folding whatever accumulated in
    // between into the next line - so a sustained exec storm (e.g. a parallel
    // build) logs once a minute, not once a tick.
    const auto now = std::chrono::steady_clock::now();
    if (!impl_->drop_log_started || now - impl_->last_drop_log >= std::chrono::minutes(1)) {
        std::fprintf(stderr,
                     "bandsightd: BPF ring buffer dropped %llu exec/exit event(s); "
                     "identity for the affected process(es) may be lost or stale\n",
                     static_cast<unsigned long long>(impl_->drops_since_log));
        impl_->drop_log_started = true;
        impl_->last_drop_log = now;
        impl_->drops_since_log = 0;
    }
}

std::vector<ProcEvent> Collector::drain_events() {
    std::vector<ProcEvent> out;
    if (degraded_) return out;

    check_dropped_events();

    if (!impl_->rb) return out;
    ring_buffer__consume(impl_->rb);  // non-blocking
    out.swap(impl_->pending);
    return out;
}

}  // namespace bandsight
