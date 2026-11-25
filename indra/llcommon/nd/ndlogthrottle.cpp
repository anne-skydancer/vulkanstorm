/**
 * $LicenseInfo:firstyear=2013&license=fsviewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (C) 2013, Nicky Dasmijn
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * The Phoenix Firestorm Project, Inc., 1831 Oakwood Drive, Fairmont, Minnesota 56031-3225 USA
 * http://www.firestormviewer.org
 * $/LicenseInfo$
 */

#include "ndlogthrottle.h"
#include <set>

#include "llpreprocessor.h"
#include "lltimer.h"


    namespace nd::logging
    {
        const U64 spamAvgInterval = 3 * 1000 * 1000; // 3 seconds
        const U64 maxThrottle = 60 * 1000 * 1000; // 1 minute
        const U64 minCallsBeforeThrottle = 250;
        const U64 initialThrottle = 5*1000*1000; // 5 seconds
        const U64 resetAfter = 30*1000*1000;

        bool logThrottleEnabled = true;

        struct CallSiteCall
        {
            char const *mFile;
            int mLine;
            mutable U64 mLastCall{ 0 };
            mutable U64 mAverage{ 0 };
            mutable U64 mCalls{ 0 };
            mutable U64 mThrottledCalls{ 0 };
            mutable U64 mLastUnthrottled{ 0 };
            mutable U32 mThrottle;

            CallSiteCall( char const *aFile, int aLine )
                : mFile( aFile )
                , mLine( aLine )
                ,
                 mThrottle( initialThrottle )
            {
            }

            bool operator< ( CallSiteCall const &aRHS ) const
            {
                if( mLine != aRHS.mLine )
                    return mLine < aRHS.mLine;

                return mFile < aRHS.mFile;
            }
        };

        bool throttle( char const *aFile, int aLine, std::ostream *aOut )
        {
            // Only called from Log::flush and protected by LogLock

            if (!logThrottleEnabled)
            {
                return false;
            }

            static std::set< CallSiteCall > stCalls;

            CallSiteCall oCall( aFile, aLine );
            oCall.mLastCall = LLTimer::getTotalTime();
            oCall.mLastUnthrottled = oCall.mLastCall;

            auto itr = stCalls.find( oCall );

            if( stCalls.end() == itr )
            {
                stCalls.insert( oCall );
                return false;
            }

            U64 nDiffT = (oCall.mLastCall - itr->mLastCall);
            U64 nDiffU = (oCall.mLastCall - itr->mLastUnthrottled);

            if( nDiffT < resetAfter )
            {
                ++itr->mCalls;
                itr->mAverage = (itr->mAverage + nDiffT )/2;
                itr->mLastCall = oCall.mLastCall;
            }
            else
            {
                itr->mCalls = 1;
                itr->mAverage = 0;
                itr->mLastCall = oCall.mLastCall;
                itr->mLastUnthrottled = oCall.mLastCall;
                itr->mThrottledCalls = 0;
                itr->mThrottle = initialThrottle;
                return false;
            }

            if( itr->mCalls > minCallsBeforeThrottle && itr->mAverage <= spamAvgInterval && nDiffU < itr->mThrottle )
            {
                ++itr->mThrottledCalls;
                return true;
            }

            itr->mLastUnthrottled = oCall.mLastCall;

            if( itr->mThrottledCalls )
            {
                if( aOut )
                    *aOut << "( Supressed " << itr->mThrottledCalls << " calls, frequency " << itr->mAverage << " usec, throttle " << itr->mThrottle << " usec ) ";

                if( itr->mThrottle < maxThrottle )
                    itr->mThrottle *= 2;
            }

            itr->mThrottledCalls = 0;

            return false;
        }

        void setThrottleEnabled(bool enabled)
        {
            logThrottleEnabled = enabled;
        }
    }


