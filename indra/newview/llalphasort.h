/** Camera-dependent ordering of the bounds used by legacy volume alpha. */
#ifndef LL_LLALPHASORT_H
#define LL_LLALPHASORT_H

#include <array>
#include <cstddef>
#include <vector>

class LLAlphaSortOrder
{
public:
    using Vector = std::array<float, 3>;
    struct Bounds { Vector center; Vector quarter_extent; };

    // Camera translation cancels between faces. Retain the size bias used by
    // LLDrawable::updateDistance when testing the camera's viewing direction.
    static float depth(const Bounds& bounds, const Vector& direction)
    {
        float result = 0.f;
        for (unsigned i = 0; i < 3; ++i)
            result += (bounds.center[i] - bounds.quarter_extent[i] * direction[i]) * direction[i];
        return result;
    }

    void append(const Bounds& bounds) { mBounds.push_back(bounds); }

    bool needsResort(const Vector& direction)
    {
        if (mChecked && direction == mLastDirection)
            return mNeedsResort;
        mChecked = true;
        mLastDirection = direction;
        mNeedsResort = false;
        for (std::size_t index = 1; index < mBounds.size(); ++index)
        {
            // Subtract before projecting to avoid a large common origin offset.
            Bounds delta;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                delta.center[axis] = mBounds[index - 1].center[axis] - mBounds[index].center[axis];
                delta.quarter_extent[axis] = mBounds[index - 1].quarter_extent[axis] - mBounds[index].quarter_extent[axis];
            }
            if (depth(delta, direction) < -0.00001f)
            {
                mNeedsResort = true;
                break;
            }
        }
        return mNeedsResort;
    }

private:
    std::vector<Bounds> mBounds;
    Vector mLastDirection{};
    bool mChecked = false;
    bool mNeedsResort = false;
};

#endif
