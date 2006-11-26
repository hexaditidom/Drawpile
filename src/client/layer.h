/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2006 Calle Laakkonen

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

#include <QGraphicsItem>

namespace drawingboard {

class Brush;

//! A modifiable pixmap item for QGraphicsScene
/**
 */
class Layer : public QGraphicsItem
{
	public:
		Layer(QGraphicsItem *parent=0, QGraphicsScene *scene=0);
		Layer(const QPixmap& pixmap, QGraphicsItem *parent=0, QGraphicsScene *scene=0);

		void setPixmap(const QPixmap& pixmap);
		QPixmap pixmap() const;

		//! Draw a line between two points with interpolated pressure values
		void drawLine(const QPoint& point1, qreal pressure1,
				const QPoint& point2, qreal pressure2, const Brush& brush);

		void drawPoint(const QPoint& point, qreal pressure, const Brush& brush);

		QRectF boundingRect() const;
		void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
			 QWidget *widget);

	private:
		void drawPoint(QPainter &painter, int x, int y, qreal pressure,
				const Brush &brush);

		QPixmap pixmap_;

		int plastx_, plasty_;
};

}

#endif

