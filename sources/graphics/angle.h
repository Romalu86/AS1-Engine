#pragma once

namespace as1
{
    class BaseStream;

    struct ANGLE
    {
        int value = 0;
        ANGLE() = default;
        explicit ANGLE(int v) : value(v) {}
        int Int() const { return value; }
        void Read(BaseStream* stream);

        static ANGLE FromXY(int x, int y, int* projectedLength = nullptr);
    };

    int AngleFromXY(int x, int y, int* projectedLength = nullptr);

    int IntegerSquareRoot(int value);
}
