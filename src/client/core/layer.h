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
#ifndef LAYER_H
#define LAYER_H

#include <QColor>

class QImage;
class QSize;

namespace dpcore {

class Brush;
class Point;
class Tile;
class LayerStack;

/**
 * @brief A drawing layer/tile manage
 * 
 * A layer is made up of multiple tiles.
 * Although images of arbitrary size can be created, the true layer size is
 * always a multiple of Tile::SIZE.
 */
class Layer {
	public:
		//! Construct a layer filled with solid color
		Layer(LayerStack *owner, int id, const QString& title, const QColor& color, const QSize& size);

		~Layer();

		//! Get the layer width in pixels
		int width() const { return width_; }

		//! Get the layer height in pixels
		int height() const { return height_; }

		//! Get the layer ID
		int id() const { return id_; }

		//! Get the layer name
		const QString& title() const { return _title; }

		//! Set the layer name
		void setTitle(const QString& title);

		//! Get the layer as an image
		QImage toImage() const;

		//! Resize this layer
		//void resize(const QSize& newsize);

		//! Get the color at the specified coordinate
		QColor colorAt(int x, int y) const;

		//! Get layer opacity
		int opacity() const { return _opacity; }

		//! Set layer opacity
		void setOpacity(int opacity);

		//! Set layer blending mode
		void setBlend(int blend);

		/**
		 * @brief Get the layer blending mode
		 * @return blending mode number
		 */
		int blendmode() const { return _blend; }

		/**
		 * @brief Is this layer hidden?
		 * Hiding a layer is slightly different than setting its opacity
		 * to zero, although the end result is the same. The hidden status
		 * is purely local: setting it will not hide the layer for other
		 * users.
		 */
		bool hidden() const { return _hidden; }

		//! Hide this layer
		void setHidden(bool hide);

		//! Draw an image onto the layer
		void putImage(int x, int y, QImage image, bool blend);

		//! Dab the layer with a brush
		void dab(int contextId, const Brush& brush, const Point& point);

		//! Draw a line using either drawHardLine or drawSoftLine
		void drawLine(int contextId, const Brush& brush, const Point& from, const Point& to, qreal &distance);

		//! Merge a sublayer with this layer
		void mergeSublayer(int id);

		//! Merge a layer
		void merge(const Layer *layer);

		//! Fill the layer with a checker pattern
		void fillChecker(const QColor& dark, const QColor& light);

		//! Fill the layer with solid color
		void fillColor(const QColor& color);

		//! Optimize layer memory usage
		void optimize();

		//! Get a tile
		const Tile *tile(int x, int y) const {
			Q_ASSERT(x>=0 && x<_xtiles);
			Q_ASSERT(y>=0 && y<_ytiles);
			return _tiles[y*_xtiles+x];
		}

		//! Get a tile
		const Tile *tile(int index) const { Q_ASSERT(index>=0 && index<_xtiles*_ytiles); return _tiles[index]; }

		//! Get the sublayers
		const QList<Layer*> &sublayers() const { return _sublayers; }

		/**
		 * @brief Is this layer visible
		 * A layer is visible when its opacity is greater than zero AND
		 * it is not explicitly hidden.
		 * @return true if layer is visible
		 */
		bool visible() const { return _opacity > 0 && !_hidden; }

	private:
		//! Construct a sublayer
		Layer(LayerStack *owner, int id, const QSize& size);

		QImage padImageToTileBoundary(int leftpad, int toppad, const QImage &original, bool alpha) const;

		//! Get a sublayer
		Layer *getSubLayer(int id, int blendmode, uchar opacity);

		void directDab(const Brush& brush, const Point& point);
		void drawHardLine(const Brush& brush, const Point& from, const Point& to, qreal &distance);
		void drawSoftLine(const Brush& brush, const Point& from, const Point& to, qreal &distance);

		LayerStack *owner_;
		int id_;
		QString _title;
	
		int width_;
		int height_;
		int _xtiles;
		int _ytiles;
		Tile **_tiles;
		uchar _opacity;
		int _blend;
		bool _hidden;

		QList<Layer*> _sublayers;
};

}

#endif

