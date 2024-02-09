sudo python3 setup.py bdist_wheel
twine upload -r testpypi dist/*.whl
sudo rm -Rf gbd_tools.egg-info dist
