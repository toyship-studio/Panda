// The MIT License(MIT)
//
// Copyright(c) 2021 Cedric Guillemet
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "GizmoPrimitives.hpp"

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <malloc.h>
#endif
#if !defined(_MSC_VER) && !defined(__MINGW64_VERSION_MAJOR)
#define _malloca(x) alloca(x)
#define _freea(x)
#endif

namespace Panda::Gizmo
{
   static const float pi = 3.14159265358979323846f;
   static const float radiansToDegrees = (180.f / pi);
   static const float degreesToRadians = (pi / 180.f);
   const float screenRotateSize = 0.06f;
   // scale a bit so translate axis do not touch when in universal
   const float rotationDisplayFactor = 1.2f;

   static Operation operator&(Operation lhs, Operation rhs)
   {
     return static_cast<Operation>(static_cast<int>(lhs) & static_cast<int>(rhs));
   }

   static bool operator!=(Operation lhs, int rhs)
   {
     return static_cast<int>(lhs) != rhs;
   }

   static bool intersects(Operation lhs, Operation rhs)
   {
     return (lhs & rhs) != 0;
   }

   // True if lhs contains rhs
   static bool contains(Operation lhs, Operation rhs)
   {
     return (lhs & rhs) == rhs;
   }

   ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   // utility and math

   void multiplyRawMatrices(const float* a, const float* b, float* r)
   {
      r[0] = a[0] * b[0] + a[1] * b[4] + a[2] * b[8] + a[3] * b[12];
      r[1] = a[0] * b[1] + a[1] * b[5] + a[2] * b[9] + a[3] * b[13];
      r[2] = a[0] * b[2] + a[1] * b[6] + a[2] * b[10] + a[3] * b[14];
      r[3] = a[0] * b[3] + a[1] * b[7] + a[2] * b[11] + a[3] * b[15];

      r[4] = a[4] * b[0] + a[5] * b[4] + a[6] * b[8] + a[7] * b[12];
      r[5] = a[4] * b[1] + a[5] * b[5] + a[6] * b[9] + a[7] * b[13];
      r[6] = a[4] * b[2] + a[5] * b[6] + a[6] * b[10] + a[7] * b[14];
      r[7] = a[4] * b[3] + a[5] * b[7] + a[6] * b[11] + a[7] * b[15];

      r[8] = a[8] * b[0] + a[9] * b[4] + a[10] * b[8] + a[11] * b[12];
      r[9] = a[8] * b[1] + a[9] * b[5] + a[10] * b[9] + a[11] * b[13];
      r[10] = a[8] * b[2] + a[9] * b[6] + a[10] * b[10] + a[11] * b[14];
      r[11] = a[8] * b[3] + a[9] * b[7] + a[10] * b[11] + a[11] * b[15];

      r[12] = a[12] * b[0] + a[13] * b[4] + a[14] * b[8] + a[15] * b[12];
      r[13] = a[12] * b[1] + a[13] * b[5] + a[14] * b[9] + a[15] * b[13];
      r[14] = a[12] * b[2] + a[13] * b[6] + a[14] * b[10] + a[15] * b[14];
      r[15] = a[12] * b[3] + a[13] * b[7] + a[14] * b[11] + a[15] * b[15];
   }

   void buildFrustum(float left, float right, float bottom, float top, float znear, float zfar, float* m16)
   {
      float temp, temp2, temp3, temp4;
      temp = 2.0f * znear;
      temp2 = right - left;
      temp3 = top - bottom;
      temp4 = zfar - znear;
      m16[0] = temp / temp2;
      m16[1] = 0.0;
      m16[2] = 0.0;
      m16[3] = 0.0;
      m16[4] = 0.0;
      m16[5] = temp / temp3;
      m16[6] = 0.0;
      m16[7] = 0.0;
      m16[8] = (right + left) / temp2;
      m16[9] = (top + bottom) / temp3;
      m16[10] = (-zfar - znear) / temp4;
      m16[11] = -1.0f;
      m16[12] = 0.0;
      m16[13] = 0.0;
      m16[14] = (-temp * zfar) / temp4;
      m16[15] = 0.0;
   }

   void buildPerspective(float fovyInDegrees, float aspectRatio, float znear, float zfar, float* m16)
   {
      float ymax, xmax;
      ymax = znear * tanf(fovyInDegrees * degreesToRadians);
      xmax = ymax * aspectRatio;
      buildFrustum(-xmax, xmax, -ymax, ymax, znear, zfar, m16);
   }

   void cross(const float* a, const float* b, float* r)
   {
      r[0] = a[1] * b[2] - a[2] * b[1];
      r[1] = a[2] * b[0] - a[0] * b[2];
      r[2] = a[0] * b[1] - a[1] * b[0];
   }

   float dot(const float* a, const float* b)
   {
      return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
   }

   void normalize(const float* a, float* r)
   {
      float il = 1.f / (sqrtf(dot(a, a)) + FLT_EPSILON);
      r[0] = a[0] * il;
      r[1] = a[1] * il;
      r[2] = a[2] * il;
   }

   void lookAt(const float* eye, const float* at, const float* up, float* m16)
   {
      float X[3], Y[3], Z[3], tmp[3];

      tmp[0] = eye[0] - at[0];
      tmp[1] = eye[1] - at[1];
      tmp[2] = eye[2] - at[2];
      normalize(tmp, Z);
      normalize(up, Y);
      cross(Y, Z, tmp);
      normalize(tmp, X);
      cross(Z, X, tmp);
      normalize(tmp, Y);

      m16[0] = X[0];
      m16[1] = Y[0];
      m16[2] = Z[0];
      m16[3] = 0.0f;
      m16[4] = X[1];
      m16[5] = Y[1];
      m16[6] = Z[1];
      m16[7] = 0.0f;
      m16[8] = X[2];
      m16[9] = Y[2];
      m16[10] = Z[2];
      m16[11] = 0.0f;
      m16[12] = -dot(X, eye);
      m16[13] = -dot(Y, eye);
      m16[14] = -dot(Z, eye);
      m16[15] = 1.0f;
   }

   template <typename T> T Clamp(T x, T y, T z) { return ((x < y) ? y : ((x > z) ? z : x)); }
   template <typename T> T max(T x, T y) { return (x > y) ? x : y; }
   template <typename T> T min(T x, T y) { return (x < y) ? x : y; }
   template <typename T> bool IsWithin(T x, T y, T z) { return (x >= y) && (x <= z); }

   struct matrix_t;
   struct vec_t
   {
   public:
      float x, y, z, w;

      void lerp(const vec_t& v, float t)
      {
         x += (v.x - x) * t;
         y += (v.y - y) * t;
         z += (v.z - z) * t;
         w += (v.w - w) * t;
      }

      void set(float v) { x = y = z = w = v; }
      void set(float _x, float _y, float _z = 0.f, float _w = 0.f) { x = _x; y = _y; z = _z; w = _w; }

      vec_t& operator -= (const vec_t& v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
      vec_t& operator += (const vec_t& v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
      vec_t& operator *= (const vec_t& v) { x *= v.x; y *= v.y; z *= v.z; w *= v.w; return *this; }
      vec_t& operator *= (float v) { x *= v;    y *= v;    z *= v;    w *= v;    return *this; }

      vec_t operator * (float f) const;
      vec_t operator - () const;
      vec_t operator - (const vec_t& v) const;
      vec_t operator + (const vec_t& v) const;
      vec_t operator * (const vec_t& v) const;

      const vec_t& operator + () const { return (*this); }
      float length() const { return sqrtf(x * x + y * y + z * z); };
      float lengthSquared() const { return (x * x + y * y + z * z); };
      vec_t normalize() { (*this) *= (1.f / ( length() > FLT_EPSILON ? length() : FLT_EPSILON ) ); return (*this); }
      vec_t normalize(const vec_t& v) { this->set(v.x, v.y, v.z, v.w); this->normalize(); return (*this); }
      vec_t absolute() const;

      void cross(const vec_t& v)
      {
         vec_t res;
         res.x = y * v.z - z * v.y;
         res.y = z * v.x - x * v.z;
         res.z = x * v.y - y * v.x;

         x = res.x;
         y = res.y;
         z = res.z;
         w = 0.f;
      }

      void cross(const vec_t& v1, const vec_t& v2)
      {
         x = v1.y * v2.z - v1.z * v2.y;
         y = v1.z * v2.x - v1.x * v2.z;
         z = v1.x * v2.y - v1.y * v2.x;
         w = 0.f;
      }

      float dot(const vec_t& v) const
      {
         return (x * v.x) + (y * v.y) + (z * v.z) + (w * v.w);
      }

      float dot3(const vec_t& v) const
      {
         return (x * v.x) + (y * v.y) + (z * v.z);
      }

      void transform(const matrix_t& matrix);
      void transform(const vec_t& s, const matrix_t& matrix);

      void transformVector(const matrix_t& matrix);
      void transformPoint(const matrix_t& matrix);
      void transformVector(const vec_t& v, const matrix_t& matrix) { (*this) = v; this->transformVector(matrix); }
      void transformPoint(const vec_t& v, const matrix_t& matrix) { (*this) = v; this->transformPoint(matrix); }

      float& operator [] (size_t index) { return ((float*)&x)[index]; }
      const float& operator [] (size_t index) const { return ((float*)&x)[index]; }
      bool operator!=(const vec_t& other) const { return memcmp(this, &other, sizeof(vec_t)) != 0; }
   };

   vec_t makeVect(float _x, float _y, float _z = 0.f, float _w = 0.f) { vec_t res; res.x = _x; res.y = _y; res.z = _z; res.w = _w; return res; }
   vec_t makeVect(ScreenPoint v) { vec_t res; res.x = v.x; res.y = v.y; res.z = 0.f; res.w = 0.f; return res; }
   vec_t vec_t::operator * (float f) const { return makeVect(x * f, y * f, z * f, w * f); }
   vec_t vec_t::operator - () const { return makeVect(-x, -y, -z, -w); }
   vec_t vec_t::operator - (const vec_t& v) const { return makeVect(x - v.x, y - v.y, z - v.z, w - v.w); }
   vec_t vec_t::operator + (const vec_t& v) const { return makeVect(x + v.x, y + v.y, z + v.z, w + v.w); }
   vec_t vec_t::operator * (const vec_t& v) const { return makeVect(x * v.x, y * v.y, z * v.z, w * v.w); }
   vec_t vec_t::absolute() const { return makeVect(fabsf(x), fabsf(y), fabsf(z)); }

   vec_t normalized(const vec_t& v) { vec_t res; res = v; res.normalize(); return res; }
   vec_t cross(const vec_t& v1, const vec_t& v2)
   {
      vec_t res;
      res.x = v1.y * v2.z - v1.z * v2.y;
      res.y = v1.z * v2.x - v1.x * v2.z;
      res.z = v1.x * v2.y - v1.y * v2.x;
      res.w = 0.f;
      return res;
   }

   float dot(const vec_t& v1, const vec_t& v2)
   {
      return (v1.x * v2.x) + (v1.y * v2.y) + (v1.z * v2.z);
   }

   vec_t buildPlan(const vec_t& p_point1, const vec_t& p_normal)
   {
      vec_t normal, res;
      normal.normalize(p_normal);
      res.w = normal.dot(p_point1);
      res.x = normal.x;
      res.y = normal.y;
      res.z = normal.z;
      return res;
   }

   struct matrix_t
   {
   public:

      union
      {
         float m[4][4];
         float m16[16];
         struct
         {
            vec_t right, up, dir, position;
         } v;
         vec_t component[4];
      };

      operator float* () { return m16; }
      operator const float* () const { return m16; }
      void translation(float _x, float _y, float _z) { this->translation(makeVect(_x, _y, _z)); }

      void translation(const vec_t& vt)
      {
         v.right.set(1.f, 0.f, 0.f, 0.f);
         v.up.set(0.f, 1.f, 0.f, 0.f);
         v.dir.set(0.f, 0.f, 1.f, 0.f);
         v.position.set(vt.x, vt.y, vt.z, 1.f);
      }

      void scaleBy(float _x, float _y, float _z)
      {
         v.right.set(_x, 0.f, 0.f, 0.f);
         v.up.set(0.f, _y, 0.f, 0.f);
         v.dir.set(0.f, 0.f, _z, 0.f);
         v.position.set(0.f, 0.f, 0.f, 1.f);
      }
      void scaleBy(const vec_t& s) { scaleBy(s.x, s.y, s.z); }

      matrix_t& operator *= (const matrix_t& mat)
      {
         matrix_t tmpMat;
         tmpMat = *this;
         tmpMat.multiply(mat);
         *this = tmpMat;
         return *this;
      }
      matrix_t operator * (const matrix_t& mat) const
      {
         matrix_t matT;
         matT.multiply(*this, mat);
         return matT;
      }

      void multiply(const matrix_t& matrix)
      {
         matrix_t tmp;
         tmp = *this;

         multiplyRawMatrices((float*)&tmp, (float*)&matrix, (float*)this);
      }

      void multiply(const matrix_t& m1, const matrix_t& m2)
      {
         multiplyRawMatrices((float*)&m1, (float*)&m2, (float*)this);
      }

      float determinant() const
      {
         return m[0][0] * m[1][1] * m[2][2] + m[0][1] * m[1][2] * m[2][0] + m[0][2] * m[1][0] * m[2][1] -
            m[0][2] * m[1][1] * m[2][0] - m[0][1] * m[1][0] * m[2][2] - m[0][0] * m[1][2] * m[2][1];
      }

      float inverse(const matrix_t& srcMatrix, bool affine = false);
      void setToIdentity()
      {
         v.right.set(1.f, 0.f, 0.f, 0.f);
         v.up.set(0.f, 1.f, 0.f, 0.f);
         v.dir.set(0.f, 0.f, 1.f, 0.f);
         v.position.set(0.f, 0.f, 0.f, 1.f);
      }
      void transpose()
      {
         matrix_t tmpm;
         for (int l = 0; l < 4; l++)
         {
            for (int c = 0; c < 4; c++)
            {
               tmpm.m[l][c] = m[c][l];
            }
         }
         (*this) = tmpm;
      }

      void rotationAxis(const vec_t& axis, float angle);

      void orthoNormalize()
      {
         v.right.normalize();
         v.up.normalize();
         v.dir.normalize();
      }
   };

   void vec_t::transform(const matrix_t& matrix)
   {
      vec_t out;

      out.x = x * matrix.m[0][0] + y * matrix.m[1][0] + z * matrix.m[2][0] + w * matrix.m[3][0];
      out.y = x * matrix.m[0][1] + y * matrix.m[1][1] + z * matrix.m[2][1] + w * matrix.m[3][1];
      out.z = x * matrix.m[0][2] + y * matrix.m[1][2] + z * matrix.m[2][2] + w * matrix.m[3][2];
      out.w = x * matrix.m[0][3] + y * matrix.m[1][3] + z * matrix.m[2][3] + w * matrix.m[3][3];

      x = out.x;
      y = out.y;
      z = out.z;
      w = out.w;
   }

   void vec_t::transform(const vec_t& s, const matrix_t& matrix)
   {
      *this = s;
      transform(matrix);
   }

   void vec_t::transformPoint(const matrix_t& matrix)
   {
      vec_t out;

      out.x = x * matrix.m[0][0] + y * matrix.m[1][0] + z * matrix.m[2][0] + matrix.m[3][0];
      out.y = x * matrix.m[0][1] + y * matrix.m[1][1] + z * matrix.m[2][1] + matrix.m[3][1];
      out.z = x * matrix.m[0][2] + y * matrix.m[1][2] + z * matrix.m[2][2] + matrix.m[3][2];
      out.w = x * matrix.m[0][3] + y * matrix.m[1][3] + z * matrix.m[2][3] + matrix.m[3][3];

      x = out.x;
      y = out.y;
      z = out.z;
      w = out.w;
   }

   void vec_t::transformVector(const matrix_t& matrix)
   {
      vec_t out;

      out.x = x * matrix.m[0][0] + y * matrix.m[1][0] + z * matrix.m[2][0];
      out.y = x * matrix.m[0][1] + y * matrix.m[1][1] + z * matrix.m[2][1];
      out.z = x * matrix.m[0][2] + y * matrix.m[1][2] + z * matrix.m[2][2];
      out.w = x * matrix.m[0][3] + y * matrix.m[1][3] + z * matrix.m[2][3];

      x = out.x;
      y = out.y;
      z = out.z;
      w = out.w;
   }

   float matrix_t::inverse(const matrix_t& srcMatrix, bool affine)
   {
      float det = 0;

      if (affine)
      {
         det = determinant();
         float s = 1 / det;
         m[0][0] = (srcMatrix.m[1][1] * srcMatrix.m[2][2] - srcMatrix.m[1][2] * srcMatrix.m[2][1]) * s;
         m[0][1] = (srcMatrix.m[2][1] * srcMatrix.m[0][2] - srcMatrix.m[2][2] * srcMatrix.m[0][1]) * s;
         m[0][2] = (srcMatrix.m[0][1] * srcMatrix.m[1][2] - srcMatrix.m[0][2] * srcMatrix.m[1][1]) * s;
         m[1][0] = (srcMatrix.m[1][2] * srcMatrix.m[2][0] - srcMatrix.m[1][0] * srcMatrix.m[2][2]) * s;
         m[1][1] = (srcMatrix.m[2][2] * srcMatrix.m[0][0] - srcMatrix.m[2][0] * srcMatrix.m[0][2]) * s;
         m[1][2] = (srcMatrix.m[0][2] * srcMatrix.m[1][0] - srcMatrix.m[0][0] * srcMatrix.m[1][2]) * s;
         m[2][0] = (srcMatrix.m[1][0] * srcMatrix.m[2][1] - srcMatrix.m[1][1] * srcMatrix.m[2][0]) * s;
         m[2][1] = (srcMatrix.m[2][0] * srcMatrix.m[0][1] - srcMatrix.m[2][1] * srcMatrix.m[0][0]) * s;
         m[2][2] = (srcMatrix.m[0][0] * srcMatrix.m[1][1] - srcMatrix.m[0][1] * srcMatrix.m[1][0]) * s;
         m[3][0] = -(m[0][0] * srcMatrix.m[3][0] + m[1][0] * srcMatrix.m[3][1] + m[2][0] * srcMatrix.m[3][2]);
         m[3][1] = -(m[0][1] * srcMatrix.m[3][0] + m[1][1] * srcMatrix.m[3][1] + m[2][1] * srcMatrix.m[3][2]);
         m[3][2] = -(m[0][2] * srcMatrix.m[3][0] + m[1][2] * srcMatrix.m[3][1] + m[2][2] * srcMatrix.m[3][2]);
      }
      else
      {
         // transpose matrix
         float src[16];
         for (int i = 0; i < 4; ++i)
         {
            src[i] = srcMatrix.m16[i * 4];
            src[i + 4] = srcMatrix.m16[i * 4 + 1];
            src[i + 8] = srcMatrix.m16[i * 4 + 2];
            src[i + 12] = srcMatrix.m16[i * 4 + 3];
         }

         // calculate pairs for first 8 elements (cofactors)
         float tmp[12]; // temp array for pairs
         tmp[0] = src[10] * src[15];
         tmp[1] = src[11] * src[14];
         tmp[2] = src[9] * src[15];
         tmp[3] = src[11] * src[13];
         tmp[4] = src[9] * src[14];
         tmp[5] = src[10] * src[13];
         tmp[6] = src[8] * src[15];
         tmp[7] = src[11] * src[12];
         tmp[8] = src[8] * src[14];
         tmp[9] = src[10] * src[12];
         tmp[10] = src[8] * src[13];
         tmp[11] = src[9] * src[12];

         // calculate first 8 elements (cofactors)
         m16[0] = (tmp[0] * src[5] + tmp[3] * src[6] + tmp[4] * src[7]) - (tmp[1] * src[5] + tmp[2] * src[6] + tmp[5] * src[7]);
         m16[1] = (tmp[1] * src[4] + tmp[6] * src[6] + tmp[9] * src[7]) - (tmp[0] * src[4] + tmp[7] * src[6] + tmp[8] * src[7]);
         m16[2] = (tmp[2] * src[4] + tmp[7] * src[5] + tmp[10] * src[7]) - (tmp[3] * src[4] + tmp[6] * src[5] + tmp[11] * src[7]);
         m16[3] = (tmp[5] * src[4] + tmp[8] * src[5] + tmp[11] * src[6]) - (tmp[4] * src[4] + tmp[9] * src[5] + tmp[10] * src[6]);
         m16[4] = (tmp[1] * src[1] + tmp[2] * src[2] + tmp[5] * src[3]) - (tmp[0] * src[1] + tmp[3] * src[2] + tmp[4] * src[3]);
         m16[5] = (tmp[0] * src[0] + tmp[7] * src[2] + tmp[8] * src[3]) - (tmp[1] * src[0] + tmp[6] * src[2] + tmp[9] * src[3]);
         m16[6] = (tmp[3] * src[0] + tmp[6] * src[1] + tmp[11] * src[3]) - (tmp[2] * src[0] + tmp[7] * src[1] + tmp[10] * src[3]);
         m16[7] = (tmp[4] * src[0] + tmp[9] * src[1] + tmp[10] * src[2]) - (tmp[5] * src[0] + tmp[8] * src[1] + tmp[11] * src[2]);

         // calculate pairs for second 8 elements (cofactors)
         tmp[0] = src[2] * src[7];
         tmp[1] = src[3] * src[6];
         tmp[2] = src[1] * src[7];
         tmp[3] = src[3] * src[5];
         tmp[4] = src[1] * src[6];
         tmp[5] = src[2] * src[5];
         tmp[6] = src[0] * src[7];
         tmp[7] = src[3] * src[4];
         tmp[8] = src[0] * src[6];
         tmp[9] = src[2] * src[4];
         tmp[10] = src[0] * src[5];
         tmp[11] = src[1] * src[4];

         // calculate second 8 elements (cofactors)
         m16[8] = (tmp[0] * src[13] + tmp[3] * src[14] + tmp[4] * src[15]) - (tmp[1] * src[13] + tmp[2] * src[14] + tmp[5] * src[15]);
         m16[9] = (tmp[1] * src[12] + tmp[6] * src[14] + tmp[9] * src[15]) - (tmp[0] * src[12] + tmp[7] * src[14] + tmp[8] * src[15]);
         m16[10] = (tmp[2] * src[12] + tmp[7] * src[13] + tmp[10] * src[15]) - (tmp[3] * src[12] + tmp[6] * src[13] + tmp[11] * src[15]);
         m16[11] = (tmp[5] * src[12] + tmp[8] * src[13] + tmp[11] * src[14]) - (tmp[4] * src[12] + tmp[9] * src[13] + tmp[10] * src[14]);
         m16[12] = (tmp[2] * src[10] + tmp[5] * src[11] + tmp[1] * src[9]) - (tmp[4] * src[11] + tmp[0] * src[9] + tmp[3] * src[10]);
         m16[13] = (tmp[8] * src[11] + tmp[0] * src[8] + tmp[7] * src[10]) - (tmp[6] * src[10] + tmp[9] * src[11] + tmp[1] * src[8]);
         m16[14] = (tmp[6] * src[9] + tmp[11] * src[11] + tmp[3] * src[8]) - (tmp[10] * src[11] + tmp[2] * src[8] + tmp[7] * src[9]);
         m16[15] = (tmp[10] * src[10] + tmp[4] * src[8] + tmp[9] * src[9]) - (tmp[8] * src[9] + tmp[11] * src[10] + tmp[5] * src[8]);

         // calculate determinant
         det = src[0] * m16[0] + src[1] * m16[1] + src[2] * m16[2] + src[3] * m16[3];

         // calculate matrix inverse
         float invdet = 1 / det;
         for (int j = 0; j < 16; ++j)
         {
            m16[j] *= invdet;
         }
      }

      return det;
   }

   void matrix_t::rotationAxis(const vec_t& axis, float angle)
   {
      float length2 = axis.lengthSquared();
      if (length2 < FLT_EPSILON)
      {
         setToIdentity();
         return;
      }

      vec_t n = axis * (1.f / sqrtf(length2));
      float s = sinf(angle);
      float c = cosf(angle);
      float k = 1.f - c;

      float xx = n.x * n.x * k + c;
      float yy = n.y * n.y * k + c;
      float zz = n.z * n.z * k + c;
      float xy = n.x * n.y * k;
      float yz = n.y * n.z * k;
      float zx = n.z * n.x * k;
      float xs = n.x * s;
      float ys = n.y * s;
      float zs = n.z * s;

      m[0][0] = xx;
      m[0][1] = xy + zs;
      m[0][2] = zx - ys;
      m[0][3] = 0.f;
      m[1][0] = xy - zs;
      m[1][1] = yy;
      m[1][2] = yz + xs;
      m[1][3] = 0.f;
      m[2][0] = zx + ys;
      m[2][1] = yz - xs;
      m[2][2] = zz;
      m[2][3] = 0.f;
      m[3][0] = 0.f;
      m[3][1] = 0.f;
      m[3][2] = 0.f;
      m[3][3] = 1.f;
   }

   ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   //

   enum MoveType
   {
      operationNone,
      operationMoveX,
      operationMoveY,
      operationMoveZ,
      operationMoveYZ,
      operationMoveZX,
      operationMoveXY,
      operationMoveScreen,
      operationRotateX,
      operationRotateY,
      operationRotateZ,
      operationRotateScreen,
      operationScaleX,
      operationScaleY,
      operationScaleZ,
      operationScaleXYZ
   };

   static bool isTranslateType(int type)
   {
     return type >= operationMoveX && type <= operationMoveScreen;
   }

   static bool isRotateType(int type)
   {
     return type >= operationRotateX && type <= operationRotateScreen;
   }

   static bool isScaleType(int type)
   {
     return type >= operationScaleX && type <= operationScaleXYZ;
   }

   // Matches paired translation plane operation order.
   static const Operation translationPlanes[3] = { translateY | translateZ, translateX | translateZ, translateX | translateY };

   enum ColorSlot
   {
      directionX,
      directionY,
      directionZ,
      planeX,
      planeY,
      planeZ,
      selection,
      translationLine,
      scaleLine,
      rotationUsingBorder,
      rotationUsingFill,
      hatchedAxisLines,
      colorCount,
   };

   struct Style
   {
      Style();

      float translationLineThickness;
      float translationLineArrowSize;
      float rotationLineThickness;
      float rotationOuterLineThickness;
      float scaleLineThickness;
      float scaleLineCircleSize;
      float hatchedAxisLineThickness;
      float centerCircleSize;

      StyleColor colors[colorCount];
   };

   Style::Style()
   {
      // default values
      translationLineThickness   = 3.0f;
      translationLineArrowSize   = 6.0f;
      rotationLineThickness      = 2.0f;
      rotationOuterLineThickness = 3.0f;
      scaleLineThickness         = 3.0f;
      scaleLineCircleSize        = 6.0f;
      hatchedAxisLineThickness   = 6.0f;
      centerCircleSize           = 6.0f;

      // initialize default colors
      colors[directionX]           = StyleColor(0.666f, 0.000f, 0.000f, 1.000f);
      colors[directionY]           = StyleColor(0.000f, 0.666f, 0.000f, 1.000f);
      colors[directionZ]           = StyleColor(0.000f, 0.000f, 0.666f, 1.000f);
      colors[planeX]               = StyleColor(0.666f, 0.000f, 0.000f, 0.380f);
      colors[planeY]               = StyleColor(0.000f, 0.666f, 0.000f, 0.380f);
      colors[planeZ]               = StyleColor(0.000f, 0.000f, 0.666f, 0.380f);
      colors[selection]             = StyleColor(1.000f, 0.500f, 0.062f, 0.541f);
      colors[translationLine]      = StyleColor(0.666f, 0.666f, 0.666f, 0.666f);
      colors[scaleLine]            = StyleColor(0.250f, 0.250f, 0.250f, 1.000f);
      colors[rotationUsingBorder] = StyleColor(1.000f, 0.500f, 0.062f, 1.000f);
      colors[rotationUsingFill]   = StyleColor(1.000f, 0.500f, 0.062f, 0.500f);
      colors[hatchedAxisLines]    = StyleColor(0.000f, 0.000f, 0.000f, 0.500f);
   }

   struct Context
   {
      Context() : mbUsing(false), mbUsingBounds(false) {}

      DrawList* mDrawList;
      Style mStyle;

      Mode mMode;
      matrix_t mViewMat;
      matrix_t mProjectionMat;
      matrix_t mModel;
      matrix_t mModelLocal; // orthonormalized model
      matrix_t mModelInverse;
      matrix_t mModelSource;
      matrix_t mModelSourceInverse;
      matrix_t mMVP;
      matrix_t mMVPLocal; // MVP with full model matrix whereas mMVP's model matrix might only be translation in case of World space edition
      matrix_t mViewProjection;

      vec_t mModelScaleOrigin;
      vec_t mCameraEye;
      vec_t mCameraRight;
      vec_t mCameraDir;
      vec_t mCameraUp;
      vec_t mRayOrigin;
      vec_t mRayVector;

      float  mRadiusSquareCenter;
      ScreenPoint mScreenSquareCenter;
      ScreenPoint mScreenSquareMin;
      ScreenPoint mScreenSquareMax;

      float mScreenFactor;
      vec_t mRelativeOrigin;

      bool mbUsing;
      bool mbMouseOver;
      bool mReversed; // reversed projection matrix

      // translation
      vec_t mTranslationPlan;
      vec_t mTranslationPlanOrigin;
      vec_t mMatrixOrigin;
      vec_t mTranslationLastDelta;

      // rotation
      vec_t mRotationVectorSource;
      float mRotationAngle;
      float mRotationAngleOrigin;
      // scale
      vec_t mScale;
      vec_t mScaleValueOrigin;
      vec_t mScaleLast;
      float mSaveMousePosx;

      // save axis factor when using gizmo
      bool mBelowAxisLimit[3];
      int mAxisMask = 0;
      bool mBelowPlaneLimit[3];
      float mAxisFactor[3];

      float mAxisLimit=0.0025f;
      float mPlaneLimit=0.02f;

      // bounds stretching
      vec_t mBoundsPivot;
      vec_t mBoundsAnchor;
      vec_t mBoundsPlan;
      vec_t mBoundsLocalPivot;
      int mBoundsBestAxis;
      int mBoundsAxis[2];
      bool mbUsingBounds;
      matrix_t mBoundsMatrix;

      //
      int mCurrentOperation;

      float mX = 0.f;
      float mY = 0.f;
      float mWidth = 0.f;
      float mHeight = 0.f;
      float mXMax = 0.f;
      float mYMax = 0.f;
      float mDisplayRatio = 1.f;

      bool mIsOrthographic = false;
      // check to not have multiple gizmo highlighted at the same time
      bool mbOverGizmoHotspot = false;

      Operation mOperation = Operation(-1);

      float mGizmoSizeClipSpace = 0.1f;
   };

   static Context gContext;

   static const vec_t directionUnary[3] = { makeVect(1.f, 0.f, 0.f), makeVect(0.f, 1.f, 0.f), makeVect(0.f, 0.f, 1.f) };
   static const float quadMin = 0.5f;
   static const float quadMax = 0.8f;
   static const float quadUV[8] = { quadMin, quadMin, quadMin, quadMax, quadMax, quadMax, quadMax, quadMin };
   static const int halfCircleSegmentCount = 64;
   static const float snapTension = 0.5f;

   ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   //
   static int getMoveType(Operation op, vec_t* gizmoHitProportion);
   static int getRotateType(Operation op);
   static int getScaleType(Operation op);

   static DrawColor getStyleColor(int idx)
   {
      return Backend::toDrawColor(gContext.mStyle.colors[idx]);
   }

   static ScreenPoint worldToPos(const vec_t& worldPos, const matrix_t& mat, ScreenPoint position = ScreenPoint(gContext.mX, gContext.mY), ScreenPoint size = ScreenPoint(gContext.mWidth, gContext.mHeight))
   {
      vec_t trans;
      trans.transformPoint(worldPos, mat);
      trans *= 0.5f / trans.w;
      trans += makeVect(0.5f, 0.5f);
      trans.y = 1.f - trans.y;
      trans.x *= size.x;
      trans.y *= size.y;
      trans.x += position.x;
      trans.y += position.y;
      return ScreenPoint(trans.x, trans.y);
   }

   static void computeCameraRay(vec_t& rayOrigin, vec_t& rayDir, ScreenPoint position = ScreenPoint(gContext.mX, gContext.mY), ScreenPoint size = ScreenPoint(gContext.mWidth, gContext.mHeight))
   {
      const InputState& io = Backend::input();

      matrix_t mViewProjInverse;
      mViewProjInverse.inverse(gContext.mViewMat * gContext.mProjectionMat);

      const float mox = ((io.mousePosition.x - position.x) / size.x) * 2.f - 1.f;
      const float moy = (1.f - ((io.mousePosition.y - position.y) / size.y)) * 2.f - 1.f;

      const float zNear = gContext.mReversed ? (1.f - FLT_EPSILON) : 0.f;
      const float zFar = gContext.mReversed ? 0.f : (1.f - FLT_EPSILON);

      rayOrigin.transform(makeVect(mox, moy, zNear, 1.f), mViewProjInverse);
      rayOrigin *= 1.f / rayOrigin.w;
      vec_t rayEnd;
      rayEnd.transform(makeVect(mox, moy, zFar, 1.f), mViewProjInverse);
      rayEnd *= 1.f / rayEnd.w;
      rayDir = normalized(rayEnd - rayOrigin);
   }

   static float getSegmentLengthClipSpace(const vec_t& start, const vec_t& end, const bool localCoordinates = false)
   {
      vec_t startOfSegment = start;
      const matrix_t& mvp = localCoordinates ? gContext.mMVPLocal : gContext.mMVP;
      startOfSegment.transformPoint(mvp);
      if (fabsf(startOfSegment.w) > FLT_EPSILON) // check for axis aligned with camera direction
      {
         startOfSegment *= 1.f / startOfSegment.w;
      }

      vec_t endOfSegment = end;
      endOfSegment.transformPoint(mvp);
      if (fabsf(endOfSegment.w) > FLT_EPSILON) // check for axis aligned with camera direction
      {
         endOfSegment *= 1.f / endOfSegment.w;
      }

      vec_t clipSpaceAxis = endOfSegment - startOfSegment;
      if (gContext.mDisplayRatio < 1.0)
         clipSpaceAxis.x *= gContext.mDisplayRatio;
      else
         clipSpaceAxis.y /= gContext.mDisplayRatio;
      float segmentLengthInClipSpace = sqrtf(clipSpaceAxis.x * clipSpaceAxis.x + clipSpaceAxis.y * clipSpaceAxis.y);
      return segmentLengthInClipSpace;
   }

   static float getParallelogram(const vec_t& ptO, const vec_t& ptA, const vec_t& ptB)
   {
      vec_t pts[] = { ptO, ptA, ptB };
      for (unsigned int i = 0; i < 3; i++)
      {
         pts[i].transformPoint(gContext.mMVP);
         if (fabsf(pts[i].w) > FLT_EPSILON) // check for axis aligned with camera direction
         {
            pts[i] *= 1.f / pts[i].w;
         }
      }
      vec_t segA = pts[1] - pts[0];
      vec_t segB = pts[2] - pts[0];
      segA.y /= gContext.mDisplayRatio;
      segB.y /= gContext.mDisplayRatio;
      vec_t segAOrtho = makeVect(-segA.y, segA.x);
      segAOrtho.normalize();
      float dt = segAOrtho.dot3(segB);
      float surface = sqrtf(segA.x * segA.x + segA.y * segA.y) * fabsf(dt);
      return surface;
   }

   inline vec_t PointOnSegment(const vec_t& point, const vec_t& vertPos1, const vec_t& vertPos2)
   {
      vec_t c = point - vertPos1;
      vec_t V;

      V.normalize(vertPos2 - vertPos1);
      float d = (vertPos2 - vertPos1).length();
      float t = V.dot3(c);

      if (t < 0.f)
      {
         return vertPos1;
      }

      if (t > d)
      {
         return vertPos2;
      }

      return vertPos1 + V * t;
   }

   static float intersectRayPlane(const vec_t& rOrigin, const vec_t& rVector, const vec_t& plan)
   {
      const float numer = plan.dot3(rOrigin) - plan.w;
      const float denom = plan.dot3(rVector);

      if (fabsf(denom) < FLT_EPSILON)  // normal is orthogonal to vector, cant intersect
      {
         return -1.0f;
      }

      return -(numer / denom);
   }

   static float distanceToPlane(const vec_t& point, const vec_t& plan)
   {
      return plan.dot3(point) + plan.w;
   }

   static bool isInContextRect(ScreenPoint p)
   {
      return IsWithin(p.x, gContext.mX, gContext.mXMax) && IsWithin(p.y, gContext.mY, gContext.mYMax);
   }

   static bool isHoveringWindow()
   {
      return Backend::isViewportHovered();
   }

   void setRect(float x, float y, float width, float height)
   {
      gContext.mX = x;
      gContext.mY = y;
      gContext.mWidth = width;
      gContext.mHeight = height;
      gContext.mXMax = gContext.mX + gContext.mWidth;
      gContext.mYMax = gContext.mY + gContext.mHeight;
      gContext.mDisplayRatio = width / height;
   }

   void setOrthographic(bool isOrthographic)
   {
      gContext.mIsOrthographic = isOrthographic;
   }

   void beginFrame()
   {
      Backend::beginFrame();
      gContext.mDrawList = &Backend::drawList();
      gContext.mbOverGizmoHotspot = false;
   }

   bool isUsing()
   {
      return (gContext.mbUsing) || gContext.mbUsingBounds;
   }

   bool isOver()
   {
      return (intersects(gContext.mOperation, translate) && getMoveType(gContext.mOperation, nullptr) != operationNone) ||
         (intersects(gContext.mOperation, rotate) && getRotateType(gContext.mOperation) != operationNone) ||
         (intersects(gContext.mOperation, scale) && getScaleType(gContext.mOperation) != operationNone) || isUsing();
   }

   static void computeContext(const float* view, const float* projection, float* matrix, Mode mode)
   {
      gContext.mMode = mode;
      gContext.mViewMat = *(matrix_t*)view;
      gContext.mProjectionMat = *(matrix_t*)projection;
      gContext.mbMouseOver = isHoveringWindow();

      gContext.mModelLocal = *(matrix_t*)matrix;
      gContext.mModelLocal.orthoNormalize();

      if (mode == local)
      {
         gContext.mModel = gContext.mModelLocal;
      }
      else
      {
         gContext.mModel.translation(((matrix_t*)matrix)->v.position);
      }
      gContext.mModelSource = *(matrix_t*)matrix;
      gContext.mModelScaleOrigin.set(gContext.mModelSource.v.right.length(), gContext.mModelSource.v.up.length(), gContext.mModelSource.v.dir.length());

      gContext.mModelInverse.inverse(gContext.mModel);
      gContext.mModelSourceInverse.inverse(gContext.mModelSource);
      gContext.mViewProjection = gContext.mViewMat * gContext.mProjectionMat;
      gContext.mMVP = gContext.mModel * gContext.mViewProjection;
      gContext.mMVPLocal = gContext.mModelLocal * gContext.mViewProjection;

      matrix_t viewInverse;
      viewInverse.inverse(gContext.mViewMat);
      gContext.mCameraDir = viewInverse.v.dir;
      gContext.mCameraEye = viewInverse.v.position;
      gContext.mCameraRight = viewInverse.v.right;
      gContext.mCameraUp = viewInverse.v.up;

      // projection reverse
       vec_t nearPos, farPos;
       nearPos.transform(makeVect(0, 0, 1.f, 1.f), gContext.mProjectionMat);
       farPos.transform(makeVect(0, 0, 2.f, 1.f), gContext.mProjectionMat);

       gContext.mReversed = (nearPos.z/nearPos.w) > (farPos.z / farPos.w);

      // compute scale from the size of camera right vector projected on screen at the matrix position
      vec_t pointRight = viewInverse.v.right;
      pointRight.transformPoint(gContext.mViewProjection);
      gContext.mScreenFactor = gContext.mGizmoSizeClipSpace / (pointRight.x / pointRight.w - gContext.mMVP.v.position.x / gContext.mMVP.v.position.w);

      vec_t rightViewInverse = viewInverse.v.right;
      rightViewInverse.transformVector(gContext.mModelInverse);
      float rightLength = getSegmentLengthClipSpace(makeVect(0.f, 0.f), rightViewInverse);
      gContext.mScreenFactor = gContext.mGizmoSizeClipSpace / rightLength;

      ScreenPoint centerSSpace = worldToPos(makeVect(0.f, 0.f), gContext.mMVP);
      gContext.mScreenSquareCenter = centerSSpace;
      gContext.mScreenSquareMin = ScreenPoint(centerSSpace.x - 10.f, centerSSpace.y - 10.f);
      gContext.mScreenSquareMax = ScreenPoint(centerSSpace.x + 10.f, centerSSpace.y + 10.f);

      computeCameraRay(gContext.mRayOrigin, gContext.mRayVector);
   }

   static void computeColors(DrawColor* colors, int type, Operation operation)
   {
      DrawColor selectionColor = getStyleColor(selection);

      switch (operation)
      {
      case translate:
         colors[0] = (type == operationMoveScreen) ? selectionColor : whiteDrawColor;
         for (int i = 0; i < 3; i++)
         {
            colors[i + 1] = (type == (int)(operationMoveX + i)) ? selectionColor : getStyleColor(directionX + i);
            colors[i + 4] = (type == (int)(operationMoveYZ + i)) ? selectionColor : getStyleColor(planeX + i);
            colors[i + 4] = (type == operationMoveScreen) ? selectionColor : colors[i + 4];
         }
         break;
      case rotate:
         colors[0] = (type == operationRotateScreen) ? selectionColor : whiteDrawColor;
         for (int i = 0; i < 3; i++)
         {
            colors[i + 1] = (type == (int)(operationRotateX + i)) ? selectionColor : getStyleColor(directionX + i);
         }
         break;
      case scaleUniversal:
      case scale:
         colors[0] = (type == operationScaleXYZ) ? selectionColor : whiteDrawColor;
         for (int i = 0; i < 3; i++)
         {
            colors[i + 1] = (type == (int)(operationScaleX + i)) ? selectionColor : getStyleColor(directionX + i);
         }
         break;
      default:
         break;
      }
   }

   static void computeTripodAxisAndVisibility(const int axisIndex, vec_t& dirAxis, vec_t& dirPlaneX, vec_t& dirPlaneY, bool& belowAxisLimit, bool& belowPlaneLimit, const bool localCoordinates = false)
   {
      dirAxis = directionUnary[axisIndex];
      dirPlaneX = directionUnary[(axisIndex + 1) % 3];
      dirPlaneY = directionUnary[(axisIndex + 2) % 3];

      if (gContext.mbUsing)
      {
         // when using, use stored factors so the gizmo doesn't flip when we translate

         // Apply axis mask to axes and planes
         belowAxisLimit = gContext.mBelowAxisLimit[axisIndex] && ((1<<axisIndex)&gContext.mAxisMask);
         belowPlaneLimit = gContext.mBelowPlaneLimit[axisIndex] && (((1<<axisIndex)&gContext.mAxisMask) && !(gContext.mAxisMask & (gContext.mAxisMask - 1)) || !gContext.mAxisMask);

         dirAxis *= gContext.mAxisFactor[axisIndex];
         dirPlaneX *= gContext.mAxisFactor[(axisIndex + 1) % 3];
         dirPlaneY *= gContext.mAxisFactor[(axisIndex + 2) % 3];
      }
      else
      {
         // new method
         float lenDir = getSegmentLengthClipSpace(makeVect(0.f, 0.f, 0.f), dirAxis, localCoordinates);
         float lenDirMinus = getSegmentLengthClipSpace(makeVect(0.f, 0.f, 0.f), -dirAxis, localCoordinates);

         float lenDirPlaneX = getSegmentLengthClipSpace(makeVect(0.f, 0.f, 0.f), dirPlaneX, localCoordinates);
         float lenDirMinusPlaneX = getSegmentLengthClipSpace(makeVect(0.f, 0.f, 0.f), -dirPlaneX, localCoordinates);

         float lenDirPlaneY = getSegmentLengthClipSpace(makeVect(0.f, 0.f, 0.f), dirPlaneY, localCoordinates);
         float lenDirMinusPlaneY = getSegmentLengthClipSpace(makeVect(0.f, 0.f, 0.f), -dirPlaneY, localCoordinates);

         const bool allowFlip = true;
         float mulAxis = (allowFlip && lenDir < lenDirMinus&& fabsf(lenDir - lenDirMinus) > FLT_EPSILON) ? -1.f : 1.f;
         float mulAxisX = (allowFlip && lenDirPlaneX < lenDirMinusPlaneX&& fabsf(lenDirPlaneX - lenDirMinusPlaneX) > FLT_EPSILON) ? -1.f : 1.f;
         float mulAxisY = (allowFlip && lenDirPlaneY < lenDirMinusPlaneY&& fabsf(lenDirPlaneY - lenDirMinusPlaneY) > FLT_EPSILON) ? -1.f : 1.f;
         dirAxis *= mulAxis;
         dirPlaneX *= mulAxisX;
         dirPlaneY *= mulAxisY;

         // for axis
         float axisLengthInClipSpace = getSegmentLengthClipSpace(makeVect(0.f, 0.f, 0.f), dirAxis * gContext.mScreenFactor, localCoordinates);

         float paraSurf = getParallelogram(makeVect(0.f, 0.f, 0.f), dirPlaneX * gContext.mScreenFactor, dirPlaneY * gContext.mScreenFactor);
         // Apply axis mask to axes and planes
         belowPlaneLimit = (paraSurf > gContext.mAxisLimit) && (((1<<axisIndex)&gContext.mAxisMask) && !(gContext.mAxisMask & (gContext.mAxisMask - 1)) || !gContext.mAxisMask);
         belowAxisLimit = (axisLengthInClipSpace > gContext.mPlaneLimit) && !((1<<axisIndex)&gContext.mAxisMask);

         // and store values
         gContext.mAxisFactor[axisIndex] = mulAxis;
         gContext.mAxisFactor[(axisIndex + 1) % 3] = mulAxisX;
         gContext.mAxisFactor[(axisIndex + 2) % 3] = mulAxisY;
         gContext.mBelowAxisLimit[axisIndex] = belowAxisLimit;
         gContext.mBelowPlaneLimit[axisIndex] = belowPlaneLimit;
      }
   }

   static void computeSnap(float* value, float snap)
   {
      if (snap <= FLT_EPSILON)
      {
         return;
      }

      float modulo = fmodf(*value, snap);
      float moduloRatio = fabsf(modulo) / snap;
      if (moduloRatio < snapTension)
      {
         *value -= modulo;
      }
      else if (moduloRatio > (1.f - snapTension))
      {
         *value = *value - modulo + snap * ((*value < 0.f) ? -1.f : 1.f);
      }
   }
   static void computeSnap(vec_t& value, const float* snap)
   {
      for (int i = 0; i < 3; i++)
      {
         computeSnap(&value[i], snap[i]);
      }
   }

   static float computeAngleOnPlan()
   {
      const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, gContext.mTranslationPlan);
      vec_t localPos = normalized(gContext.mRayOrigin + gContext.mRayVector * len - gContext.mModel.v.position);

      vec_t perpendicularVector;
      perpendicularVector.cross(gContext.mRotationVectorSource, gContext.mTranslationPlan);
      perpendicularVector.normalize();
      float acosAngle = Clamp(dot(localPos, gContext.mRotationVectorSource), -1.f, 1.f);
      float angle = acosf(acosAngle);
      angle *= (dot(localPos, perpendicularVector) < 0.f) ? 1.f : -1.f;
      return angle;
   }

   static void drawRotationGizmo(Operation op, int type)
   {
      if(!intersects(op, rotate))
      {
         return;
      }
      DrawList* drawList = gContext.mDrawList;

      bool isMultipleAxesMasked = gContext.mAxisMask & (gContext.mAxisMask - 1);
      bool isNoAxesMasked = !gContext.mAxisMask;

      // colors
      DrawColor colors[7];
      computeColors(colors, type, rotate);

      vec_t cameraToModelNormalized;
      if (gContext.mIsOrthographic)
      {
         matrix_t viewInverse;
         viewInverse.inverse(*(matrix_t*)&gContext.mViewMat);
         cameraToModelNormalized = -viewInverse.v.dir;
      }
      else
      {
         cameraToModelNormalized = normalized(gContext.mModel.v.position - gContext.mCameraEye);
      }

      cameraToModelNormalized.transformVector(gContext.mModelInverse);

      gContext.mRadiusSquareCenter = screenRotateSize * gContext.mHeight;

      bool hasRSC = intersects(op, rotateScreen);
      for (int axis = 0; axis < 3; axis++)
      {
         if(!intersects(op, static_cast<Operation>(rotateZ >> axis)))
         {
            continue;
         }

         bool isAxisMasked = (1 << (2 - axis)) & gContext.mAxisMask;

         if ((!isAxisMasked || isMultipleAxesMasked) && !isNoAxesMasked)
         {
            continue;
         }
         const bool usingAxis = (gContext.mbUsing && type == operationRotateZ - axis);
         const int circleMul = (hasRSC && !usingAxis) ? 1 : 2;

         ScreenPoint* circlePos = (ScreenPoint*)alloca(sizeof(ScreenPoint) * (circleMul * halfCircleSegmentCount + 1));

         float angleStart = atan2f(cameraToModelNormalized[(4 - axis) % 3], cameraToModelNormalized[(3 - axis) % 3]) + pi * 0.5f;

         for (int i = 0; i < circleMul * halfCircleSegmentCount + 1; i++)
         {
            float ng = angleStart + (float)circleMul * pi * ((float)i / (float)(circleMul * halfCircleSegmentCount));
            vec_t axisPos = makeVect(cosf(ng), sinf(ng), 0.f);
            vec_t pos = makeVect(axisPos[axis], axisPos[(axis + 1) % 3], axisPos[(axis + 2) % 3]) * gContext.mScreenFactor * rotationDisplayFactor;
            circlePos[i] = worldToPos(pos, gContext.mMVP);
         }
         if (!gContext.mbUsing || usingAxis)
         {
            drawList->addPolyline(circlePos, circleMul* halfCircleSegmentCount + 1, colors[3 - axis], false, gContext.mStyle.rotationLineThickness);
         }

         float radiusAxis = sqrtf((lengthSquared(worldToPos(gContext.mModel.v.position, gContext.mViewProjection) - circlePos[0])));
         if (radiusAxis > gContext.mRadiusSquareCenter)
         {
            gContext.mRadiusSquareCenter = radiusAxis;
         }
      }
      if(hasRSC && (!gContext.mbUsing || type == operationRotateScreen) && (!isMultipleAxesMasked && isNoAxesMasked))
      {
         drawList->addCircle(worldToPos(gContext.mModel.v.position, gContext.mViewProjection), gContext.mRadiusSquareCenter, colors[0], 64, gContext.mStyle.rotationOuterLineThickness);
      }

      if (gContext.mbUsing && isRotateType(type))
      {
         ScreenPoint circlePos[halfCircleSegmentCount + 1];

         circlePos[0] = worldToPos(gContext.mModel.v.position, gContext.mViewProjection);
         for (unsigned int i = 1; i < halfCircleSegmentCount + 1; i++)
         {
            float ng = gContext.mRotationAngle * ((float)(i - 1) / (float)(halfCircleSegmentCount - 1));
            matrix_t rotateVectorMatrix;
            rotateVectorMatrix.rotationAxis(gContext.mTranslationPlan, ng);
            vec_t pos;
            pos.transformPoint(gContext.mRotationVectorSource, rotateVectorMatrix);
            pos *= gContext.mScreenFactor * rotationDisplayFactor;
            circlePos[i] = worldToPos(pos + gContext.mModel.v.position, gContext.mViewProjection);
         }
         drawList->addConvexPolyFilled(circlePos, halfCircleSegmentCount + 1, getStyleColor(rotationUsingFill));
         drawList->addPolyline(circlePos, halfCircleSegmentCount + 1, getStyleColor(rotationUsingBorder), true, gContext.mStyle.rotationLineThickness);

      }
   }

   static void drawHatchedAxis(const vec_t& axis)
   {
      if (gContext.mStyle.hatchedAxisLineThickness <= 0.0f)
      {
         return;
      }

      for (int j = 1; j < 10; j++)
      {
         ScreenPoint baseSSpace2 = worldToPos(axis * 0.05f * (float)(j * 2) * gContext.mScreenFactor, gContext.mMVP);
         ScreenPoint worldDirSSpace2 = worldToPos(axis * 0.05f * (float)(j * 2 + 1) * gContext.mScreenFactor, gContext.mMVP);
         gContext.mDrawList->addLine(baseSSpace2, worldDirSSpace2, getStyleColor(hatchedAxisLines), gContext.mStyle.hatchedAxisLineThickness);
      }
   }

   static void drawScaleGizmo(Operation op, int type)
   {
      DrawList* drawList = gContext.mDrawList;

      if(!intersects(op, scale))
      {
        return;
      }

      // colors
      DrawColor colors[7];
      computeColors(colors, type, scale);

      // draw
      vec_t scaleDisplay = { 1.f, 1.f, 1.f, 1.f };

      if (gContext.mbUsing)
      {
         scaleDisplay = gContext.mScale;
      }

      for (int i = 0; i < 3; i++)
      {
         if(!intersects(op, static_cast<Operation>(scaleX << i)))
         {
            continue;
         }
         const bool usingAxis = (gContext.mbUsing && type == operationScaleX + i);
         if (!gContext.mbUsing || usingAxis)
         {
            vec_t dirPlaneX, dirPlaneY, dirAxis;
            bool belowAxisLimit, belowPlaneLimit;
            computeTripodAxisAndVisibility(i, dirAxis, dirPlaneX, dirPlaneY, belowAxisLimit, belowPlaneLimit, true);

            // draw axis
            if (belowAxisLimit)
            {
               bool hasTranslateOnAxis = contains(op, static_cast<Operation>(translateX << i));
               float markerScale = hasTranslateOnAxis ? 1.4f : 1.0f;
               ScreenPoint baseSSpace = worldToPos(dirAxis * 0.1f * gContext.mScreenFactor, gContext.mMVP);
               ScreenPoint worldDirSSpaceNoScale = worldToPos(dirAxis * markerScale * gContext.mScreenFactor, gContext.mMVP);
               ScreenPoint worldDirSSpace = worldToPos((dirAxis * markerScale * scaleDisplay[i]) * gContext.mScreenFactor, gContext.mMVP);

               if (gContext.mbUsing)
               {
                  DrawColor scaleLineColor = getStyleColor(scaleLine);
                  drawList->addLine(baseSSpace, worldDirSSpaceNoScale, scaleLineColor, gContext.mStyle.scaleLineThickness);
                  drawList->addCircleFilled(worldDirSSpaceNoScale, gContext.mStyle.scaleLineCircleSize, scaleLineColor);
               }

               if (!hasTranslateOnAxis || gContext.mbUsing)
               {
                  drawList->addLine(baseSSpace, worldDirSSpace, colors[i + 1], gContext.mStyle.scaleLineThickness);
               }
               drawList->addCircleFilled(worldDirSSpace, gContext.mStyle.scaleLineCircleSize, colors[i + 1]);

               if (gContext.mAxisFactor[i] < 0.f)
               {
                  drawHatchedAxis(dirAxis * scaleDisplay[i]);
               }
            }
         }
      }

      // draw screen cirle
      drawList->addCircleFilled(gContext.mScreenSquareCenter, gContext.mStyle.centerCircleSize, colors[0], 32);

   }


   static void drawScaleUniversalGizmo(Operation op, int type)
   {
      DrawList* drawList = gContext.mDrawList;

      if (!intersects(op, scaleUniversal))
      {
         return;
      }

      // colors
      DrawColor colors[7];
      computeColors(colors, type, scaleUniversal);

      // draw
      vec_t scaleDisplay = { 1.f, 1.f, 1.f, 1.f };

      if (gContext.mbUsing)
      {
         scaleDisplay = gContext.mScale;
      }

      for (int i = 0; i < 3; i++)
      {
         if (!intersects(op, static_cast<Operation>(scaleXUniversal << i)))
         {
            continue;
         }
         const bool usingAxis = (gContext.mbUsing && type == operationScaleX + i);
         if (!gContext.mbUsing || usingAxis)
         {
            vec_t dirPlaneX, dirPlaneY, dirAxis;
            bool belowAxisLimit, belowPlaneLimit;
            computeTripodAxisAndVisibility(i, dirAxis, dirPlaneX, dirPlaneY, belowAxisLimit, belowPlaneLimit, true);

            // draw axis
            if (belowAxisLimit)
            {
               bool hasTranslateOnAxis = contains(op, static_cast<Operation>(translateX << i));
               float markerScale = hasTranslateOnAxis ? 1.4f : 1.0f;
               ScreenPoint worldDirSSpace = worldToPos((dirAxis * markerScale * scaleDisplay[i]) * gContext.mScreenFactor, gContext.mMVPLocal);
               drawList->addCircleFilled(worldDirSSpace, 12.f, colors[i + 1]);
            }
         }
      }

      // draw screen cirle
      drawList->addCircle(gContext.mScreenSquareCenter, 20.f, colors[0], 32, gContext.mStyle.centerCircleSize);

   }

   static void drawTranslationGizmo(Operation op, int type)
   {
      DrawList* drawList = gContext.mDrawList;
      if (!drawList)
      {
         return;
      }

      if(!intersects(op, translate))
      {
         return;
      }

      // colors
      DrawColor colors[7];
      computeColors(colors, type, translate);

      const ScreenPoint origin = worldToPos(gContext.mModel.v.position, gContext.mViewProjection);

      // draw
      bool belowAxisLimit = false;
      bool belowPlaneLimit = false;
      for (int i = 0; i < 3; ++i)
      {
         vec_t dirPlaneX, dirPlaneY, dirAxis;
         computeTripodAxisAndVisibility(i, dirAxis, dirPlaneX, dirPlaneY, belowAxisLimit, belowPlaneLimit);

         if (!gContext.mbUsing || (gContext.mbUsing && type == operationMoveX + i))
         {
            // draw axis
            if (belowAxisLimit && intersects(op, static_cast<Operation>(translateX << i)))
            {
               ScreenPoint baseSSpace = worldToPos(dirAxis * 0.1f * gContext.mScreenFactor, gContext.mMVP);
               ScreenPoint worldDirSSpace = worldToPos(dirAxis * gContext.mScreenFactor, gContext.mMVP);

               drawList->addLine(baseSSpace, worldDirSSpace, colors[i + 1], gContext.mStyle.translationLineThickness);

               // Arrow head begin
               ScreenPoint dir(origin - worldDirSSpace);

               float d = sqrtf(lengthSquared(dir));
               dir /= d; // normalize
               dir *= gContext.mStyle.translationLineArrowSize;

               ScreenPoint ortogonalDir(dir.y, -dir.x); // Perpendicular vector
               ScreenPoint a(worldDirSSpace + dir);
               drawList->addTriangleFilled(worldDirSSpace - dir, a + ortogonalDir, a - ortogonalDir, colors[i + 1]);
               // Arrow head end

               if (gContext.mAxisFactor[i] < 0.f)
               {
                  drawHatchedAxis(dirAxis);
               }
            }
         }
         // draw plane
         if (!gContext.mbUsing || (gContext.mbUsing && type == operationMoveYZ + i))
         {
            if (belowPlaneLimit && contains(op, translationPlanes[i]))
            {
               ScreenPoint screenQuadPts[4];
               for (int j = 0; j < 4; ++j)
               {
                  vec_t cornerWorldPos = (dirPlaneX * quadUV[j * 2] + dirPlaneY * quadUV[j * 2 + 1]) * gContext.mScreenFactor;
                  screenQuadPts[j] = worldToPos(cornerWorldPos, gContext.mMVP);
               }
               drawList->addPolyline(screenQuadPts, 4, getStyleColor(directionX + i), true, 1.0f);
               drawList->addConvexPolyFilled(screenQuadPts, 4, colors[i + 4]);
            }
         }
      }

      drawList->addCircleFilled(gContext.mScreenSquareCenter, gContext.mStyle.centerCircleSize, colors[0], 32);

      if (gContext.mbUsing && isTranslateType(type))
      {
         DrawColor translationLineColor = getStyleColor(translationLine);

         ScreenPoint sourcePosOnScreen = worldToPos(gContext.mMatrixOrigin, gContext.mViewProjection);
         ScreenPoint destinationPosOnScreen = worldToPos(gContext.mModel.v.position, gContext.mViewProjection);
         vec_t dif = { destinationPosOnScreen.x - sourcePosOnScreen.x, destinationPosOnScreen.y - sourcePosOnScreen.y, 0.f, 0.f };
         dif.normalize();
         dif *= 5.f;
         drawList->addCircle(sourcePosOnScreen, 6.f, translationLineColor);
         drawList->addCircle(destinationPosOnScreen, 6.f, translationLineColor);
         drawList->addLine(ScreenPoint(sourcePosOnScreen.x + dif.x, sourcePosOnScreen.y + dif.y), ScreenPoint(destinationPosOnScreen.x - dif.x, destinationPosOnScreen.y - dif.y), translationLineColor, 2.f);

      }
   }

   static bool canActivate()
   {
      if (Backend::isMouseClicked() && !Backend::isBlockedByUi())
      {
         return true;
      }
      return false;
   }

   static void handleAndDrawLocalBounds(const float* bounds, matrix_t* matrix, const float* snapValues, Operation operation)
   {
      const InputState& io = Backend::input();
      DrawList* drawList = gContext.mDrawList;

      // compute best projection axis
      vec_t axesWorldDirections[3];
      vec_t bestAxisWorldDirection = { 0.0f, 0.0f, 0.0f, 0.0f };
      int axes[3];
      unsigned int numAxes = 1;
      axes[0] = gContext.mBoundsBestAxis;
      int bestAxis = axes[0];
      if (!gContext.mbUsingBounds)
      {
         numAxes = 0;
         float bestDot = 0.f;
         for (int i = 0; i < 3; i++)
         {
            vec_t dirPlaneNormalWorld;
            dirPlaneNormalWorld.transformVector(directionUnary[i], gContext.mModelSource);
            dirPlaneNormalWorld.normalize();

            float dt = fabsf(dot(normalized(gContext.mCameraEye - gContext.mModelSource.v.position), dirPlaneNormalWorld));
            if (dt >= bestDot)
            {
               bestDot = dt;
               bestAxis = i;
               bestAxisWorldDirection = dirPlaneNormalWorld;
            }

            if (dt >= 0.1f)
            {
               axes[numAxes] = i;
               axesWorldDirections[numAxes] = dirPlaneNormalWorld;
               ++numAxes;
            }
         }
      }

      if (numAxes == 0)
      {
         axes[0] = bestAxis;
         axesWorldDirections[0] = bestAxisWorldDirection;
         numAxes = 1;
      }

      else if (bestAxis != axes[0])
      {
         unsigned int bestIndex = 0;
         for (unsigned int i = 0; i < numAxes; i++)
         {
            if (axes[i] == bestAxis)
            {
               bestIndex = i;
               break;
            }
         }
         int tempAxis = axes[0];
         axes[0] = axes[bestIndex];
         axes[bestIndex] = tempAxis;
         vec_t tempDirection = axesWorldDirections[0];
         axesWorldDirections[0] = axesWorldDirections[bestIndex];
         axesWorldDirections[bestIndex] = tempDirection;
      }

      for (unsigned int axisIndex = 0; axisIndex < numAxes; ++axisIndex)
      {
         bestAxis = axes[axisIndex];
         bestAxisWorldDirection = axesWorldDirections[axisIndex];

         // corners
         vec_t aabb[4];

         int secondAxis = (bestAxis + 1) % 3;
         int thirdAxis = (bestAxis + 2) % 3;

         for (int i = 0; i < 4; i++)
         {
            aabb[i][3] = aabb[i][bestAxis] = 0.f;
            aabb[i][secondAxis] = bounds[secondAxis + 3 * (i >> 1)];
            aabb[i][thirdAxis] = bounds[thirdAxis + 3 * ((i >> 1) ^ (i & 1))];
         }

         // draw bounds
         unsigned int anchorAlpha = blackDrawColor;

         matrix_t boundsMVP = gContext.mModelSource * gContext.mViewProjection;
         for (int i = 0; i < 4; i++)
         {
            ScreenPoint worldBound1 = worldToPos(aabb[i], boundsMVP);
            ScreenPoint worldBound2 = worldToPos(aabb[(i + 1) % 4], boundsMVP);
            if (!isInContextRect(worldBound1) || !isInContextRect(worldBound2))
            {
               continue;
            }
            float boundDistance = sqrtf(lengthSquared(worldBound1 - worldBound2));
            int stepCount = (int)(boundDistance / 10.f);
            stepCount = min(stepCount, 1000);
            for (int j = 0; j < stepCount; j++)
            {
               float stepLength = 1.f / (float)stepCount;
               float t1 = (float)j * stepLength;
               float t2 = (float)j * stepLength + stepLength * 0.5f;
               ScreenPoint worldBoundSS1 = lerp(worldBound1, worldBound2, ScreenPoint(t1, t1));
               ScreenPoint worldBoundSS2 = lerp(worldBound1, worldBound2, ScreenPoint(t2, t2));
               drawList->addLine(worldBoundSS1, worldBoundSS2, makeDrawColor(0xAA, 0xAA, 0xAA, 0) + anchorAlpha, 2.f);
            }
            vec_t midPoint = (aabb[i] + aabb[(i + 1) % 4]) * 0.5f;
            ScreenPoint midBound = worldToPos(midPoint, boundsMVP);
            static const float AnchorBigRadius = 8.f;
            static const float AnchorSmallRadius = 6.f;
            bool overBigAnchor = lengthSquared(worldBound1 - io.mousePosition) <= (AnchorBigRadius * AnchorBigRadius);
            bool overSmallAnchor = lengthSquared(midBound - io.mousePosition) <= (AnchorBigRadius * AnchorBigRadius);

            int type = operationNone;
            vec_t gizmoHitProportion;

            if(intersects(operation, translate))
            {
               type = getMoveType(operation, &gizmoHitProportion);
            }
            if(intersects(operation, rotate) && type == operationNone)
            {
               type = getRotateType(operation);
            }
            if(intersects(operation, scale) && type == operationNone)
            {
               type = getScaleType(operation);
            }

            if (type != operationNone)
            {
               overBigAnchor = false;
               overSmallAnchor = false;
            }

            DrawColor selectionColor = getStyleColor(selection);

            unsigned int bigAnchorColor = overBigAnchor ? selectionColor : (makeDrawColor(0xAA, 0xAA, 0xAA, 0) + anchorAlpha);
            unsigned int smallAnchorColor = overSmallAnchor ? selectionColor : (makeDrawColor(0xAA, 0xAA, 0xAA, 0) + anchorAlpha);

            drawList->addCircleFilled(worldBound1, AnchorBigRadius, blackDrawColor);
            drawList->addCircleFilled(worldBound1, AnchorBigRadius - 1.2f, bigAnchorColor);

            drawList->addCircleFilled(midBound, AnchorSmallRadius, blackDrawColor);
            drawList->addCircleFilled(midBound, AnchorSmallRadius - 1.2f, smallAnchorColor);
            int oppositeIndex = (i + 2) % 4;
            // big anchor on corners
            if (!gContext.mbUsingBounds && overBigAnchor && canActivate())
            {
               gContext.mBoundsPivot.transformPoint(aabb[(i + 2) % 4], gContext.mModelSource);
               gContext.mBoundsAnchor.transformPoint(aabb[i], gContext.mModelSource);
               gContext.mBoundsPlan = buildPlan(gContext.mBoundsAnchor, bestAxisWorldDirection);
               gContext.mBoundsBestAxis = bestAxis;
               gContext.mBoundsAxis[0] = secondAxis;
               gContext.mBoundsAxis[1] = thirdAxis;

               gContext.mBoundsLocalPivot.set(0.f);
               gContext.mBoundsLocalPivot[secondAxis] = aabb[oppositeIndex][secondAxis];
               gContext.mBoundsLocalPivot[thirdAxis] = aabb[oppositeIndex][thirdAxis];

               gContext.mbUsingBounds = true;
               gContext.mBoundsMatrix = gContext.mModelSource;
            }
            // small anchor on middle of segment
            if (!gContext.mbUsingBounds && overSmallAnchor && canActivate())
            {
               vec_t midPointOpposite = (aabb[(i + 2) % 4] + aabb[(i + 3) % 4]) * 0.5f;
               gContext.mBoundsPivot.transformPoint(midPointOpposite, gContext.mModelSource);
               gContext.mBoundsAnchor.transformPoint(midPoint, gContext.mModelSource);
               gContext.mBoundsPlan = buildPlan(gContext.mBoundsAnchor, bestAxisWorldDirection);
               gContext.mBoundsBestAxis = bestAxis;
               int indices[] = { secondAxis , thirdAxis };
               gContext.mBoundsAxis[0] = indices[i % 2];
               gContext.mBoundsAxis[1] = -1;

               gContext.mBoundsLocalPivot.set(0.f);
               gContext.mBoundsLocalPivot[gContext.mBoundsAxis[0]] = aabb[oppositeIndex][indices[i % 2]];

               gContext.mbUsingBounds = true;
               gContext.mBoundsMatrix = gContext.mModelSource;
            }
         }

         if (gContext.mbUsingBounds)
         {
            matrix_t scale;
            scale.setToIdentity();

            // compute projected mouse position on plan
            const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, gContext.mBoundsPlan);
            vec_t newPos = gContext.mRayOrigin + gContext.mRayVector * len;

            // compute a reference and delta vectors base on mouse move
            vec_t deltaVector = (newPos - gContext.mBoundsPivot).absolute();
            vec_t referenceVector = (gContext.mBoundsAnchor - gContext.mBoundsPivot).absolute();

            // for 1 or 2 axes, compute a ratio that's used for scale and snap it based on resulting length
            for (int i = 0; i < 2; i++)
            {
               int axisIndex1 = gContext.mBoundsAxis[i];
               if (axisIndex1 == -1)
               {
                  continue;
               }

               float ratioAxis = 1.f;
               vec_t axisDir = gContext.mBoundsMatrix.component[axisIndex1].absolute();

               float dtAxis = axisDir.dot(referenceVector);
               float boundSize = bounds[axisIndex1 + 3] - bounds[axisIndex1];
               if (dtAxis > FLT_EPSILON)
               {
                  ratioAxis = axisDir.dot(deltaVector) / dtAxis;
               }

               if (snapValues)
               {
                  float length = boundSize * ratioAxis;
                  computeSnap(&length, snapValues[axisIndex1]);
                  if (boundSize > FLT_EPSILON)
                  {
                     ratioAxis = length / boundSize;
                  }
               }
               scale.component[axisIndex1] *= ratioAxis;
            }

            // transform matrix
            matrix_t preScale, postScale;
            preScale.translation(-gContext.mBoundsLocalPivot);
            postScale.translation(gContext.mBoundsLocalPivot);
            matrix_t res = preScale * scale * postScale * gContext.mBoundsMatrix;
            *matrix = res;

         }

         if (!io.mouseDown) {
            gContext.mbUsingBounds = false;
         }
         if (gContext.mbUsingBounds)
         {
            break;
         }
      }
   }

   ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   //

   static int getScaleType(Operation op)
   {
      if (gContext.mbUsing)
      {
         return operationNone;
      }
      const InputState& io = Backend::input();
      int type = operationNone;

      // screen
      if (io.mousePosition.x >= gContext.mScreenSquareMin.x && io.mousePosition.x <= gContext.mScreenSquareMax.x &&
         io.mousePosition.y >= gContext.mScreenSquareMin.y && io.mousePosition.y <= gContext.mScreenSquareMax.y &&
         contains(op, scale))
      {
         type = operationScaleXYZ;
      }

      // compute
      for (int i = 0; i < 3 && type == operationNone; i++)
      {
         if(!intersects(op, static_cast<Operation>(scaleX << i)))
         {
            continue;
         }
         bool isAxisMasked = (1 << i) & gContext.mAxisMask;

         vec_t dirPlaneX, dirPlaneY, dirAxis;
         bool belowAxisLimit, belowPlaneLimit;
         computeTripodAxisAndVisibility(i, dirAxis, dirPlaneX, dirPlaneY, belowAxisLimit, belowPlaneLimit, true);
         dirAxis.transformVector(gContext.mModelLocal);
         dirPlaneX.transformVector(gContext.mModelLocal);
         dirPlaneY.transformVector(gContext.mModelLocal);

         const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, buildPlan(gContext.mModelLocal.v.position, dirAxis));
         vec_t posOnPlan = gContext.mRayOrigin + gContext.mRayVector * len;

         const float startOffset = contains(op, static_cast<Operation>(translateX << i)) ? 1.0f : 0.1f;
         const float endOffset = contains(op, static_cast<Operation>(translateX << i)) ? 1.4f : 1.0f;
         const ScreenPoint posOnPlanScreen = worldToPos(posOnPlan, gContext.mViewProjection);
         const ScreenPoint axisStartOnScreen = worldToPos(gContext.mModelLocal.v.position + dirAxis * gContext.mScreenFactor * startOffset, gContext.mViewProjection);
         const ScreenPoint axisEndOnScreen = worldToPos(gContext.mModelLocal.v.position + dirAxis * gContext.mScreenFactor * endOffset, gContext.mViewProjection);

         vec_t closestPointOnAxis = PointOnSegment(makeVect(posOnPlanScreen), makeVect(axisStartOnScreen), makeVect(axisEndOnScreen));

         if ((closestPointOnAxis - makeVect(posOnPlanScreen)).length() < 12.f) // pixel size
         {
            if (!isAxisMasked)
               type = operationScaleX + i;
         }
      }

      // universal

      vec_t deltaScreen = { io.mousePosition.x - gContext.mScreenSquareCenter.x, io.mousePosition.y - gContext.mScreenSquareCenter.y, 0.f, 0.f };
      float dist = deltaScreen.length();
      if (contains(op, scaleUniversal) && dist >= 17.0f && dist < 23.0f)
      {
         type = operationScaleXYZ;
      }

      for (int i = 0; i < 3 && type == operationNone; i++)
      {
         if (!intersects(op, static_cast<Operation>(scaleXUniversal << i)))
         {
            continue;
         }

         vec_t dirPlaneX, dirPlaneY, dirAxis;
         bool belowAxisLimit, belowPlaneLimit;
         computeTripodAxisAndVisibility(i, dirAxis, dirPlaneX, dirPlaneY, belowAxisLimit, belowPlaneLimit, true);

         // draw axis
         if (belowAxisLimit)
         {
            bool hasTranslateOnAxis = contains(op, static_cast<Operation>(translateX << i));
            float markerScale = hasTranslateOnAxis ? 1.4f : 1.0f;
            ScreenPoint worldDirSSpace = worldToPos((dirAxis * markerScale) * gContext.mScreenFactor, gContext.mMVPLocal);

            float distance = sqrtf(lengthSquared(worldDirSSpace - io.mousePosition));
            if (distance < 12.f)
            {
               type = operationScaleX + i;
            }
         }
      }
      return type;
   }

   static int getRotateType(Operation op)
   {
      if (gContext.mbUsing)
      {
         return operationNone;
      }

      bool isNoAxesMasked = !gContext.mAxisMask;
      bool isMultipleAxesMasked = gContext.mAxisMask & (gContext.mAxisMask - 1);

      const InputState& io = Backend::input();
      int type = operationNone;

      vec_t deltaScreen = { io.mousePosition.x - gContext.mScreenSquareCenter.x, io.mousePosition.y - gContext.mScreenSquareCenter.y, 0.f, 0.f };
      float dist = deltaScreen.length();
      if (intersects(op, rotateScreen) && dist >= (gContext.mRadiusSquareCenter - 4.0f) && dist < (gContext.mRadiusSquareCenter + 4.0f))
      {
         if (!isNoAxesMasked)
            return operationNone;
         type = operationRotateScreen;
      }

      const vec_t planNormals[] = { gContext.mModel.v.right, gContext.mModel.v.up, gContext.mModel.v.dir };

      vec_t modelViewPos;
      modelViewPos.transformPoint(gContext.mModel.v.position, gContext.mViewMat);

      for (int i = 0; i < 3 && type == operationNone; i++)
      {
         if(!intersects(op, static_cast<Operation>(rotateX << i)))
         {
            continue;
         }
         bool isAxisMasked = (1 << i) & gContext.mAxisMask;
         // pickup plan
         vec_t pickupPlan = buildPlan(gContext.mModel.v.position, planNormals[i]);

         const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, pickupPlan);
         const vec_t intersectWorldPos = gContext.mRayOrigin + gContext.mRayVector * len;
         vec_t intersectViewPos;
         intersectViewPos.transformPoint(intersectWorldPos, gContext.mViewMat);

         if (absolute(modelViewPos.z) - absolute(intersectViewPos.z) < -FLT_EPSILON)
         {
            continue;
         }

         const vec_t localPos = intersectWorldPos - gContext.mModel.v.position;
         vec_t idealPosOnCircle = normalized(localPos);
         idealPosOnCircle.transformVector(gContext.mModelInverse);
         const ScreenPoint idealPosOnCircleScreen = worldToPos(idealPosOnCircle * rotationDisplayFactor * gContext.mScreenFactor, gContext.mMVP);

         //gContext.mDrawList->addCircle(idealPosOnCircleScreen, 5.f, whiteDrawColor);
         const ScreenPoint distanceOnScreen = idealPosOnCircleScreen - io.mousePosition;

         const float distance = makeVect(distanceOnScreen).length();
         if (distance < 8.f) // pixel size
         {
            if ((!isAxisMasked || isMultipleAxesMasked) && !isNoAxesMasked)
               break;
            type = operationRotateX + i;
         }
      }

      return type;
   }

   static int getMoveType(Operation op, vec_t* gizmoHitProportion)
   {
      if(!intersects(op, translate) || gContext.mbUsing || !gContext.mbMouseOver)
      {
        return operationNone;
      }

      bool isNoAxesMasked = !gContext.mAxisMask;
      bool isMultipleAxesMasked = gContext.mAxisMask & (gContext.mAxisMask - 1);

      const InputState& io = Backend::input();
      int type = operationNone;

      // screen
      if (io.mousePosition.x >= gContext.mScreenSquareMin.x && io.mousePosition.x <= gContext.mScreenSquareMax.x &&
         io.mousePosition.y >= gContext.mScreenSquareMin.y && io.mousePosition.y <= gContext.mScreenSquareMax.y &&
         contains(op, translate))
      {
         type = operationMoveScreen;
      }

      const vec_t screenCoord = makeVect(io.mousePosition - ScreenPoint(gContext.mX, gContext.mY));

      // compute
      for (int i = 0; i < 3 && type == operationNone; i++)
      {
         bool isAxisMasked = (1 << i) & gContext.mAxisMask;
         vec_t dirPlaneX, dirPlaneY, dirAxis;
         bool belowAxisLimit, belowPlaneLimit;
         computeTripodAxisAndVisibility(i, dirAxis, dirPlaneX, dirPlaneY, belowAxisLimit, belowPlaneLimit);
         dirAxis.transformVector(gContext.mModel);
         dirPlaneX.transformVector(gContext.mModel);
         dirPlaneY.transformVector(gContext.mModel);

         const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, buildPlan(gContext.mModel.v.position, dirAxis));
         vec_t posOnPlan = gContext.mRayOrigin + gContext.mRayVector * len;

         const ScreenPoint axisStartOnScreen = worldToPos(gContext.mModel.v.position + dirAxis * gContext.mScreenFactor * 0.1f, gContext.mViewProjection) - ScreenPoint(gContext.mX, gContext.mY);
         const ScreenPoint axisEndOnScreen = worldToPos(gContext.mModel.v.position + dirAxis * gContext.mScreenFactor, gContext.mViewProjection) - ScreenPoint(gContext.mX, gContext.mY);

         vec_t closestPointOnAxis = PointOnSegment(screenCoord, makeVect(axisStartOnScreen), makeVect(axisEndOnScreen));
         if ((closestPointOnAxis - screenCoord).length() < 12.f && intersects(op, static_cast<Operation>(translateX << i))) // pixel size
         {
            if (isAxisMasked)
               break;
            type = operationMoveX + i;
         }

         const float dx = dirPlaneX.dot3((posOnPlan - gContext.mModel.v.position) * (1.f / gContext.mScreenFactor));
         const float dy = dirPlaneY.dot3((posOnPlan - gContext.mModel.v.position) * (1.f / gContext.mScreenFactor));
         if (belowPlaneLimit && dx >= quadUV[0] && dx <= quadUV[4] && dy >= quadUV[1] && dy <= quadUV[3] && contains(op, translationPlanes[i]))
         {
            if ((!isAxisMasked || isMultipleAxesMasked) && !isNoAxesMasked)
               break;
            type = operationMoveYZ + i;
         }

         if (gizmoHitProportion)
         {
            *gizmoHitProportion = makeVect(dx, dy, 0.f);
         }
      }
      return type;
   }

   static bool handleTranslation(float* matrix, float* deltaMatrix, Operation op, int& type, const float* snap)
   {
      if(!intersects(op, translate) || type != operationNone)
      {
        return false;
      }
      const InputState& io = Backend::input();
      const bool applyRotationLocaly = gContext.mMode == local || type == operationMoveScreen;
      bool modified = false;

      // move
      if (gContext.mbUsing && isTranslateType(gContext.mCurrentOperation))
      {
         const float signedLength = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, gContext.mTranslationPlan);
         const float len = fabsf(signedLength); // near plan
         const vec_t newPos = gContext.mRayOrigin + gContext.mRayVector * len;

         // compute delta
         const vec_t newOrigin = newPos - gContext.mRelativeOrigin * gContext.mScreenFactor;
         vec_t delta = newOrigin - gContext.mModel.v.position;

         // 1 axis constraint
         if (gContext.mCurrentOperation >= operationMoveX && gContext.mCurrentOperation <= operationMoveZ)
         {
            const int axisIndex = gContext.mCurrentOperation - operationMoveX;
            const vec_t& axisValue = *(vec_t*)&gContext.mModel.m[axisIndex];
            const float lengthOnAxis = dot(axisValue, delta);
            delta = axisValue * lengthOnAxis;
         }

         // snap
         if (snap)
         {
            vec_t cumulativeDelta = gContext.mModel.v.position + delta - gContext.mMatrixOrigin;
            if (applyRotationLocaly)
            {
               matrix_t modelSourceNormalized = gContext.mModelSource;
               modelSourceNormalized.orthoNormalize();
               matrix_t modelSourceNormalizedInverse;
               modelSourceNormalizedInverse.inverse(modelSourceNormalized);
               cumulativeDelta.transformVector(modelSourceNormalizedInverse);
               computeSnap(cumulativeDelta, snap);
               cumulativeDelta.transformVector(modelSourceNormalized);
            }
            else
            {
               computeSnap(cumulativeDelta, snap);
            }
            delta = gContext.mMatrixOrigin + cumulativeDelta - gContext.mModel.v.position;

         }

         if (delta != gContext.mTranslationLastDelta)
         {
            modified = true;
         }
         gContext.mTranslationLastDelta = delta;

         // compute matrix & delta
         matrix_t deltaMatrixTranslation;
         deltaMatrixTranslation.translation(delta);
         if (deltaMatrix)
         {
            memcpy(deltaMatrix, deltaMatrixTranslation.m16, sizeof(float) * 16);
         }

         const matrix_t res = gContext.mModelSource * deltaMatrixTranslation;
         *(matrix_t*)matrix = res;

         if (!io.mouseDown)
         {
            gContext.mbUsing = false;
         }

         type = gContext.mCurrentOperation;
      }
      else
      {
         // find new possible way to move
         vec_t gizmoHitProportion;
         type = gContext.mbOverGizmoHotspot ? operationNone : getMoveType(op, &gizmoHitProportion);
         gContext.mbOverGizmoHotspot |= type != operationNone;
         if (type != operationNone)
         {
         }
         if (canActivate() && type != operationNone)
         {
            gContext.mbUsing = true;
            gContext.mCurrentOperation = type;
            vec_t movePlanNormal[] = { gContext.mModel.v.right, gContext.mModel.v.up, gContext.mModel.v.dir,
               gContext.mModel.v.right, gContext.mModel.v.up, gContext.mModel.v.dir,
               -gContext.mCameraDir };

            vec_t cameraToModelNormalized = normalized(gContext.mModel.v.position - gContext.mCameraEye);
            for (unsigned int i = 0; i < 3; i++)
            {
               vec_t orthoVector = cross(movePlanNormal[i], cameraToModelNormalized);
               movePlanNormal[i].cross(orthoVector);
               movePlanNormal[i].normalize();
            }
            // pickup plan
            gContext.mTranslationPlan = buildPlan(gContext.mModel.v.position, movePlanNormal[type - operationMoveX]);
            const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, gContext.mTranslationPlan);
            gContext.mTranslationPlanOrigin = gContext.mRayOrigin + gContext.mRayVector * len;
            gContext.mMatrixOrigin = gContext.mModel.v.position;

            gContext.mRelativeOrigin = (gContext.mTranslationPlanOrigin - gContext.mModel.v.position) * (1.f / gContext.mScreenFactor);
         }
      }
      return modified;
   }

   static bool handleScale(float* matrix, float* deltaMatrix, Operation op, int& type, const float* snap)
   {
      if((!intersects(op, scale) && !intersects(op, scaleUniversal)) || type != operationNone || !gContext.mbMouseOver)
      {
         return false;
      }
      const InputState& io = Backend::input();
      bool modified = false;

      if (!gContext.mbUsing)
      {
         // find new possible way to scale
         type = gContext.mbOverGizmoHotspot ? operationNone : getScaleType(op);
         gContext.mbOverGizmoHotspot |= type != operationNone;

         if (type != operationNone)
         {
         }
         if (canActivate() && type != operationNone)
         {
            gContext.mbUsing = true;
            gContext.mCurrentOperation = type;
            const vec_t movePlanNormal[] = { gContext.mModelLocal.v.up, gContext.mModelLocal.v.dir, gContext.mModelLocal.v.right, gContext.mModelLocal.v.dir, gContext.mModelLocal.v.up, gContext.mModelLocal.v.right, -gContext.mCameraDir };
            // pickup plan

            gContext.mTranslationPlan = buildPlan(gContext.mModelLocal.v.position, movePlanNormal[type - operationScaleX]);
            const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, gContext.mTranslationPlan);
            gContext.mTranslationPlanOrigin = gContext.mRayOrigin + gContext.mRayVector * len;
            gContext.mMatrixOrigin = gContext.mModelLocal.v.position;
            gContext.mScale.set(1.f, 1.f, 1.f);
            gContext.mRelativeOrigin = (gContext.mTranslationPlanOrigin - gContext.mModelLocal.v.position) * (1.f / gContext.mScreenFactor);
            gContext.mScaleValueOrigin = makeVect(gContext.mModelSource.v.right.length(), gContext.mModelSource.v.up.length(), gContext.mModelSource.v.dir.length());
            gContext.mSaveMousePosx = io.mousePosition.x;
         }
      }
      // scale
      if (gContext.mbUsing && isScaleType(gContext.mCurrentOperation))
      {
         const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, gContext.mTranslationPlan);
         vec_t newPos = gContext.mRayOrigin + gContext.mRayVector * len;
         vec_t newOrigin = newPos - gContext.mRelativeOrigin * gContext.mScreenFactor;
         vec_t delta = newOrigin - gContext.mModelLocal.v.position;

         // 1 axis constraint
         if (gContext.mCurrentOperation >= operationScaleX && gContext.mCurrentOperation <= operationScaleZ)
         {
            int axisIndex = gContext.mCurrentOperation - operationScaleX;
            const vec_t& axisValue = *(vec_t*)&gContext.mModelLocal.m[axisIndex];
            float lengthOnAxis = dot(axisValue, delta);
            delta = axisValue * lengthOnAxis;

            vec_t baseVector = gContext.mTranslationPlanOrigin - gContext.mModelLocal.v.position;
            float ratio = dot(axisValue, baseVector + delta) / dot(axisValue, baseVector);

            gContext.mScale[axisIndex] = max(ratio, 0.001f);
         }
         else
         {
            float scaleDelta = (io.mousePosition.x - gContext.mSaveMousePosx) * 0.01f;
            gContext.mScale.set(max(1.f + scaleDelta, 0.001f));
         }

         // snap
         if (snap)
         {
            float scaleSnap[] = { snap[0], snap[0], snap[0] };
            computeSnap(gContext.mScale, scaleSnap);
         }

         // no 0 allowed
         for (int i = 0; i < 3; i++)
            gContext.mScale[i] = max(gContext.mScale[i], 0.001f);

         if (gContext.mScaleLast != gContext.mScale)
         {
            modified = true;
         }
         gContext.mScaleLast = gContext.mScale;

         // compute matrix & delta
         matrix_t deltaMatrixScale;
         deltaMatrixScale.scaleBy(gContext.mScale * gContext.mScaleValueOrigin);

         matrix_t res = deltaMatrixScale * gContext.mModelLocal;
         *(matrix_t*)matrix = res;

         if (deltaMatrix)
         {
            vec_t deltaScale = gContext.mScale * gContext.mScaleValueOrigin;

            vec_t originalScaleDivider;
            originalScaleDivider.x = 1 / gContext.mModelScaleOrigin.x;
            originalScaleDivider.y = 1 / gContext.mModelScaleOrigin.y;
            originalScaleDivider.z = 1 / gContext.mModelScaleOrigin.z;

            deltaScale = deltaScale * originalScaleDivider;

            deltaMatrixScale.scaleBy(deltaScale);
            memcpy(deltaMatrix, deltaMatrixScale.m16, sizeof(float) * 16);
         }

         if (!io.mouseDown)
         {
            gContext.mbUsing = false;
            gContext.mScale.set(1.f, 1.f, 1.f);
         }

         type = gContext.mCurrentOperation;
      }
      return modified;
   }

   static bool handleRotation(float* matrix, float* deltaMatrix, Operation op, int& type, const float* snap)
   {
      if(!intersects(op, rotate) || type != operationNone || !gContext.mbMouseOver)
      {
        return false;
      }
      const InputState& io = Backend::input();
      bool applyRotationLocaly = gContext.mMode == local;
      bool modified = false;

      if (!gContext.mbUsing)
      {
         type = gContext.mbOverGizmoHotspot ? operationNone : getRotateType(op);
         gContext.mbOverGizmoHotspot |= type != operationNone;

         if (type != operationNone)
         {
         }

         if (type == operationRotateScreen)
         {
            applyRotationLocaly = true;
         }

         if (canActivate() && type != operationNone)
         {
            gContext.mbUsing = true;
            gContext.mCurrentOperation = type;
            const vec_t rotatePlanNormal[] = { gContext.mModel.v.right, gContext.mModel.v.up, gContext.mModel.v.dir, -gContext.mCameraDir };
            // pickup plan
            if (applyRotationLocaly)
            {
               gContext.mTranslationPlan = buildPlan(gContext.mModel.v.position, rotatePlanNormal[type - operationRotateX]);
            }
            else
            {
               gContext.mTranslationPlan = buildPlan(gContext.mModelSource.v.position, directionUnary[type - operationRotateX]);
            }

            const float len = intersectRayPlane(gContext.mRayOrigin, gContext.mRayVector, gContext.mTranslationPlan);
            vec_t localPos = gContext.mRayOrigin + gContext.mRayVector * len - gContext.mModel.v.position;
            gContext.mRotationVectorSource = normalized(localPos);
            gContext.mRotationAngleOrigin = computeAngleOnPlan();
         }
      }

      // rotation
      if (gContext.mbUsing && isRotateType(gContext.mCurrentOperation))
      {
         gContext.mRotationAngle = computeAngleOnPlan();
         if (snap)
         {
            float snapInRadian = snap[0] * degreesToRadians;
            computeSnap(&gContext.mRotationAngle, snapInRadian);
         }
         vec_t rotationAxisLocalSpace;

         rotationAxisLocalSpace.transformVector(makeVect(gContext.mTranslationPlan.x, gContext.mTranslationPlan.y, gContext.mTranslationPlan.z, 0.f), gContext.mModelInverse);
         rotationAxisLocalSpace.normalize();

         matrix_t deltaRotation;
         deltaRotation.rotationAxis(rotationAxisLocalSpace, gContext.mRotationAngle - gContext.mRotationAngleOrigin);
         if (gContext.mRotationAngle != gContext.mRotationAngleOrigin)
         {
            modified = true;
         }
         gContext.mRotationAngleOrigin = gContext.mRotationAngle;

         matrix_t scaleOrigin;
         scaleOrigin.scaleBy(gContext.mModelScaleOrigin);

         if (applyRotationLocaly)
         {
            *(matrix_t*)matrix = scaleOrigin * deltaRotation * gContext.mModelLocal;
         }
         else
         {
            matrix_t res = gContext.mModelSource;
            res.v.position.set(0.f);

            *(matrix_t*)matrix = res * deltaRotation;
            ((matrix_t*)matrix)->v.position = gContext.mModelSource.v.position;
         }

         if (deltaMatrix)
         {
            *(matrix_t*)deltaMatrix = gContext.mModelInverse * deltaRotation * gContext.mModel;
         }

         if (!io.mouseDown)
         {
            gContext.mbUsing = false;
         }
         type = gContext.mCurrentOperation;
      }
      return modified;
   }

   void decomposeMatrixToComponents(const float* matrix, float* translation, float* rotation, float* scale)
   {
      matrix_t mat = *(matrix_t*)matrix;

      scale[0] = mat.v.right.length();
      scale[1] = mat.v.up.length();
      scale[2] = mat.v.dir.length();

      mat.orthoNormalize();

      rotation[0] = radiansToDegrees * atan2f(mat.m[1][2], mat.m[2][2]);
      rotation[1] = radiansToDegrees * atan2f(-mat.m[0][2], sqrtf(mat.m[1][2] * mat.m[1][2] + mat.m[2][2] * mat.m[2][2]));
      rotation[2] = radiansToDegrees * atan2f(mat.m[0][1], mat.m[0][0]);

      translation[0] = mat.v.position.x;
      translation[1] = mat.v.position.y;
      translation[2] = mat.v.position.z;
   }

   void recomposeMatrixFromComponents(const float* translation, const float* rotation, const float* scale, float* matrix)
   {
      matrix_t& mat = *(matrix_t*)matrix;

      matrix_t rot[3];
      for (int i = 0; i < 3; i++)
      {
         rot[i].rotationAxis(directionUnary[i], rotation[i] * degreesToRadians);
      }

      mat = rot[0] * rot[1] * rot[2];

      float validScale[3];
      for (int i = 0; i < 3; i++)
      {
         if (fabsf(scale[i]) < FLT_EPSILON)
         {
            validScale[i] = 0.001f;
         }
         else
         {
            validScale[i] = scale[i];
         }
      }
      mat.v.right *= validScale[0];
      mat.v.up *= validScale[1];
      mat.v.dir *= validScale[2];
      mat.v.position.set(translation[0], translation[1], translation[2], 1.f);
   }

   bool manipulate(const float* view, const float* projection, Operation operation, Mode mode, float* matrix, float* deltaMatrix, const float* snap, const float* localBounds, const float* boundsSnap)
   {
      // scaleBy is always local or matrix will be skewed when applying world scale or oriented matrix
      computeContext(view, projection, matrix, (operation & scale) ? local : mode);

      // set delta to identity
      if (deltaMatrix)
      {
         ((matrix_t*)deltaMatrix)->setToIdentity();
      }

      // behind camera
      vec_t camSpacePosition;
      camSpacePosition.transformPoint(makeVect(0.f, 0.f, 0.f), gContext.mMVP);
      if (!gContext.mIsOrthographic && camSpacePosition.z < 0.001f && !gContext.mbUsing)
      {
         return false;
      }

      // --
      int type = operationNone;
      bool manipulated = false;
      if (!gContext.mbUsingBounds)
      {
         manipulated = handleTranslation(matrix, deltaMatrix, operation, type, snap) ||
                       handleScale(matrix, deltaMatrix, operation, type, snap) ||
                       handleRotation(matrix, deltaMatrix, operation, type, snap);
      }

      if (localBounds && !gContext.mbUsing)
      {
         handleAndDrawLocalBounds(localBounds, (matrix_t*)matrix, boundsSnap, operation);
      }

      gContext.mOperation = operation;
      if (!gContext.mbUsingBounds)
      {
         drawRotationGizmo(operation, type);
         drawTranslationGizmo(operation, type);
         drawScaleGizmo(operation, type);
         drawScaleUniversalGizmo(operation, type);
      }

      return manipulated;
   }

}
