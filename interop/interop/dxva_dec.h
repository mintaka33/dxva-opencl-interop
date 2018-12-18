#pragma once

#include "utils.h"
#include "dxva_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <memory.h>

#include <iostream>
#include <string>
#include <map>
#include <vector>

using namespace std;

#define FREE_RESOURCE(res) \
    if(res) {res->Release(); res = NULL;}

int dxvaDecode();

