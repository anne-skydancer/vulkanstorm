// Compile-only coverage for the actual statistics and unit templates.
#include "linden_common.h"
#include "lltrace.h"

namespace
{
template<typename T>
void exercise()
{
    LLTrace::EventStatHandle<T> event("compile_event");
    LLTrace::SampleStatHandle<T> sample("compile_sample");
    LLTrace::CountStatHandle<T> count("compile_count");
    const T value(1);
    LLTrace::record(event, value);
    LLTrace::sample(sample, value);
    LLTrace::add(count, value);
    (void)event.getUnitLabel();
    (void)sample.getUnitLabel();
    (void)count.getUnitLabel();
    (void)count.getTotalSamples();
    (void)count.getTotalSampleCount();
}
}

// Deliberately never executed: instantiate production code with native headers,
// compiler flags and dependencies, without initializing global viewer state.
void lltrace_compile_type_coverage()
{
    exercise<S32>();
    exercise<U32>();
    exercise<S64>();
    exercise<U64>();
    exercise<F32>();
    exercise<F64>();
    exercise<S32Bytes>();
    exercise<U32Milliseconds>();
    exercise<F32Seconds>();
    exercise<F64Milliseconds>();
    exercise<F64Seconds>();
    exercise<F64Kilobits>();
    exercise<F64Kilobytes>();
    exercise<F64Megabytes>();
    LLTrace::SampleStatHandle<bool> flag("compile_bool");
    LLTrace::sample(flag, true);
}
