/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2007-2013 Calle Laakkonen

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
#include <QApplication>
#include <QVariant>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringList>

#include "palette.h"

Palette::Palette(const QString& name, const QString& filename)
	: name_(name), filename_(filename), modified_(false)
{
	if(filename_.isEmpty())
		filename_ = QString("%1.gpl").arg(name);
}

/**
 * Load a palette from a GIMP palette file.
 * @param filename palette file name
 */
Palette *Palette::fromFile(const QFileInfo& file)
{
	QFile palfile(file.absoluteFilePath());
	if (!palfile.open(QIODevice::ReadOnly | QIODevice::Text))
		return 0;

	QTextStream in(&palfile);
	if(in.readLine() != "GIMP Palette")
		return 0;
	QString line = in.readLine();
	if(line.startsWith("Name:")) {
		Palette *pal = new Palette(line.mid(5).trimmed(),file.fileName());

		int index = 0;
		QRegExp whitespace("\\s+");
		while (!in.atEnd()) {
			line = in.readLine();
			if(line.isEmpty() || line[0] == '#')
				continue;
			QStringList tokens = line.split(whitespace);
			if(tokens.count() != 4) // ignore unknown lines
				continue;
			pal->insertColor(index++, QColor(
					tokens[0].toInt(),
					tokens[1].toInt(),
					tokens[2].toInt()
					)
				);
		}
		pal->modified_ = false;
		return pal;
	}
	return 0;
}

/**
 * @param filename palette file name
 */
bool Palette::save(const QString& filename)
{
	QFile data(filename);
	if (data.open(QFile::WriteOnly | QFile::Truncate)) {
		QTextStream out(&data);
		out << "GIMP Palette\nName: " << name_ << "\n#\n";
		foreach(QColor color, colors_) {
			out << color.red() << ' ' << color.green() << ' ' << color.blue() << "\tUntitled\n";
		}
		modified_ = false;
		return true;
	}
	return false;
}


/**
 * Generates a palette with some predefined colors.
 * @return a new palette
 */
Palette *Palette::makeDefaultPalette()
{
	Palette *pal = new Palette(QApplication::tr("Default"));

	for(int value=25;value<255;value+=25) {
		for(int hue=0;hue<345;hue+=35)
			pal->insertColor(0, QColor::fromHsv(hue,255,value));
	}
	return pal;
}

/**
 * Change the palette name.
 * The filename is set as the name + extension ".gpl"
 * @param name new palette name
 */
void Palette::setName(const QString& name)
{
	name_ = name;
	filename_ = QString("%1.gpl").arg(name);
}

int Palette::count() const
{
	return colors_.count();
}

/**
 * @param index color index
 * @return color at specified index
 * @pre 0 <= index < count()
*/
QColor Palette::color(int index) const
{
	return colors_.at(index);
}

/**
 * Size of the palette is increased by one. The new color is
 * inserted before the index. If index == count(), the color is
 * added to the end of the palette.
 * @param index color index
 * @param color color
 * @pre 0 <= index <= count()
*/
void Palette::setColor(int index, const QColor& color)
{
	colors_[index] = color;
	modified_ = true;
}

void Palette::insertColor(int index, const QColor& color)
{
	colors_.insert(index, color);
	modified_ = true;
}

/**
 * Size of the palette is decreased by one.
 * @param index color index
 * @pre 0 <= index < count()
*/
void Palette::removeColor(int index)
{
	colors_.removeAt(index);
	modified_ = true;
}

