#!/usr/bin/env python3
# Copyright (C) 2017 Privacore ApS - https://www.privacore.com
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# License TL;DR: If you change this file, you must publish your changes.

from selenium import webdriver
from selenium.webdriver.support.ui import WebDriverWait
import argparse
import sys


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('url', default='', nargs='?', help='Web page to get')
    parser.add_argument('--user-agent', default='GigablastOpenSource/1.0', help='User agent to use')
    parser.add_argument('--input-file', help='Input file with user agent and url as content')

    args = parser.parse_args()

    options = webdriver.ChromeOptions()
    options.add_argument('headless')

    if args.input_file:
        with open(args.input_file) as f:
            user_agent = f.readline().strip()
            url = f.readline().strip()
    else:
        user_agent = args.user_agent
        url = args.url

    if not len(url):
        parser.error('either url or input-file must be given as input')

    # set user-agent
    if len(user_agent):
        options.add_argument('user-agent=%s' % user_agent)

    driver = webdriver.Chrome(chrome_options=options)

    driver.set_page_load_timeout(10)

    result = True
    try:
        driver.get(url)

        print(driver.current_url)
        print(driver.page_source)

    except Exception as e:
        print(e)
        result = False

    driver.quit()

    if not result:
        sys.exit(1)
