/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2008-2013 Calle Laakkonen

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/
#ifndef TILE_H
#define TILE_H

#include <QPixmap>

class QColor;
class QImage;
class QPainter;

namespace dpcore {

/**
 * @brief A piece of an image
 * Each tile is a square of size SIZE*SIZE. The pixel format is 32-bit ARGB.
 *
 */
class Tile {
	public:
		//! The tile width and height
		static const int SIZE = 64;

		//! The length of the tile data in bytes
		static const int BYTES = SIZE * SIZE * sizeof(quint32);

		/** @brief Round i upwards to SIZE boundary
		 * @param i coordinate
		 * @return i rounded up to nearest multiple of SIZE
		 */
		static int roundUp(int i) {
			return (i + SIZE-1)/SIZE*SIZE;
		}
		
		/**
		 * @brief Round i down to SIZE boundary
		 * @param i coordinate
		 * @return i rounded down to nearest multiple of SIZE
		 */
		static int roundDown(int i) {
			return (i/SIZE) * SIZE;
		}

		//! Construct a blank tile
		Tile(const QColor& color, int x, int y);

		//! Construct a tile from an image
		Tile(const QImage& image, int x, int y, int xoff=0, int yoff=0);

		//! Construct a copy of the given tile
		Tile(const Tile *src);

		//! Construct an empty tile
		Tile(int x, int y);

		//! Get tile X index
		int x() const { return x_; }

		//! Get tile Y index
		int y() const { return y_; }

		//! Get a pixel value from this tile
		quint32 pixel(int x, int y) const {
			Q_ASSERT(x>=0 && x<SIZE);
			Q_ASSERT(y>=0 && y<SIZE);
			return *(data_ + y * SIZE + x);
		}

		//! Composite values multiplied by color onto this tile
		void composite(int mode, const uchar *values, const QColor& color, int x, int y, int w, int h, int offset);

		//! Composite another tile with this tile
		void merge(const Tile *tile, uchar opacity, int blend);

		//! Copy the contents of this tile onto the appropriate spot on an image
		void copyToImage(QImage& image) const;

		//! Copy the contents of this tile onto the given spot on an image
		void copyToImage(QImage& image, int x, int y) const;

		//! Fill this tile with a checker pattern
		void fillChecker(const QColor& dark, const QColor& light);

		//! Fill this tile with a solid color
		void fillColor(const QColor& color);

		//! Get read access to the raw pixel data
		const quint32 *data() const { return data_; }

		//! Check if this tile is completely transparent
		bool isBlank() const;

		//! Fill a tile sized memory buffer with a checker pattenr
		static void fillChecker(quint32 *data, const QColor& dark, const QColor& light);

	private:
		int x_, y_;
		quint32 data_[SIZE*SIZE];
};

}

#endif

