#!/bin/sh
# Review the cron tasks defined in the system and warn the admin
# if some of the files will not be run
#
# This program is copyright 2011 by Javier Fernandez-Sanguino <jfs@debian.org>
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# For more information please see
#  http://www.gnu.org/licenses/licenses.html#GPL


set -e 
# reset locale, just in case
LC_ALL=C
export LC_ALL

warn () {
# Send a warning to the user
    file=$1
    reason=$2

    name=`basename $file`
    # Skip hidden files
    echo $name | grep -q -E '^\.' && return

    # Do not send a warning if the file is '.dpkg-old' or '.dpkg-dist'
    if ! echo $file | grep -q -E '\.dpkg-(old|dist)$' ; then
        echo "WARN: The file $file will not be executed by cron: $reason"
    else
        echo "INFO: The file $file is a leftover from the Debian package manager"
    fi
}

# Step 1: Review /etc/cron.d

# First: check if we are using -l
[ -r /etc/default/cron ] &&  . /etc/default/cron

# Now review the scripts, note that cron does not use run-parts to run these
# so they are *not* required to be executables, just to conform with the 

temp=`tempfile` || { echo "ERROR: Cannot create temporary file" >&2 ; exit 1; }

if [ "$LSBNAMES" = "-l" ] ; then
    run-parts ---lsbsysinit --list /etc/cron.d >$temp
else
    run-parts --list /etc/cron.d >$temp
fi

# Now check the files we found and the ones that exist in the directory
find /etc/cron.d \( -type f -o -type l \) -printf '%p %l\n'  |
while read file pointer; do
    if ! grep -q "^$file$" $temp; then
            warn $file "Does not conform to the run-parts convention"
    else
        if [ -L "$file" ] ; then
# for symlinks: does the file exist?
            if [ ! -e "$pointer" ] ; then
                 warn $file "Points to an unexistant location ($pointer)"
             fi
# for symlinks: is it owned by root?
            owner=`ls -l $pointer  | awk '{print $3}'`
            if [ "$owner" != "root" ]; then
                 warn $file "Is not owned by root"
            fi
        fi
    fi
done

