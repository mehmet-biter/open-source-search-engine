import pytest
import mimetypes
import os


@pytest.mark.parametrize('filename, is_default_page, expected_title', [
    # filename                                  is_default_page                expected_title

    # test pdf
    ('data/office/title_word_no_prop.pdf',      False,  'title_word_no_prop.pdf'),
    ('data/office/title_word_no_prop.pdf',      True,   ''),
    ('data/office/title_word_with_prop.pdf',    False,  'Title for Microsoft Word (in title)'),

    # test emoticon
    ('data/html/title_emoticon_start.html',     False,  'The quick brown fox jumps over the lazy dog'),
    ('data/html/title_emoticon_middle.html',    False,  'The quick brown fox jumps over the lazy dog'),
    ('data/html/title_emoticon_end.html',       False,  'The quick brown fox jumps over the lazy dog'),
])
def test_title(gb_api, httpserver, filename, is_default_page, expected_title):
    httpserver.serve_content(content=open(filename, 'rb').read(),
                             headers={'content-type': mimetypes.guess_type(filename)[0]})

    # format url
    file_url = httpserver.url + '/'
    if not is_default_page:
        file_url += os.path.basename(filename)

    # add url
    assert gb_api.add_url(file_url) == True

    # verify result
    result = gb_api.search('url:' + file_url)
    assert len(result['results']) == 1
    assert result['results'][0]['title'] == expected_title
