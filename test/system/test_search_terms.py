import pytest


@pytest.mark.parametrize('query, expected_terms', [
    # query                 expected
    ('the one',             ['the one', 'the', 'one', '1']),
    ('to be or not to be',  ['to be', 'be or', 'or not', 'not to', 'to', 'be', 'or']),
    ('html',                ['html', 'Hypertext Markup Language'])
])
def test_search_terms(gb_api, query, expected_terms):
    result = gb_api.search(query)

    result_terms = []
    for result_term in result['queryInfo']['terms']:
        result_terms.append(result_term['termStr'])

    for expected_term in expected_terms:
        assert expected_term in result_terms