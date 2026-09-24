/*
 * Clipboard.h - the clipboard for clips, notes etc.
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of MXM (Musica ex Machina), a fork of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef MXM_CLIPBOARD_H
#define MXM_CLIPBOARD_H

#include <QDomElement>
#include <QMap>

#include "mxm_export.h"

class QMimeData;

namespace mxm::Clipboard
{

	enum class MimeType
	{
		StringPair,
		Default
	};

	// Convenience Methods
	const QMimeData * getMimeData();
	bool hasFormat( MimeType mT );

	// Helper methods for String data
	void MXM_EXPORT copyString(const QString& str, MimeType mT);
	QString getString( MimeType mT );

	// Helper methods for String Pair data
	void copyStringPair( const QString & key, const QString & value );
	QString decodeKey( const QMimeData * mimeData );
	QString decodeValue( const QMimeData * mimeData );

	inline const char * mimeType( MimeType type )
	{
		switch( type )
		{
			case MimeType::StringPair:
				return "application/x-mxm-stringpair";
			break;
			case MimeType::Default:
			default:
				return "application/x-mxm-clipboard";
				break;
		}
	}

} // namespace mxm::Clipboard

#endif // MXM_CLIPBOARD_H
