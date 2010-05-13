/*
 *  OneDReader.cpp
 *  ZXing
 *
 *  Created by Lukasz Warchol on 10-01-15.
 *  Copyright 2010 ZXing authors All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "OneDReader.h"
#include <zxing/ReaderException.h>
#include <math.h>
#include <limits.h>

namespace zxing {
	namespace oned {
		using namespace std;
		
		OneDReader::OneDReader() {
		}
		
		Ref<Result> OneDReader::decode(Ref<BinaryBitmap> image) {
			try {
				return doDecode(image);
			}catch (ReaderException re) {
				if (false /*tryHarder && image.isRotateSupported()*/) {
					/*
					BinaryBitmap rotatedImage = image.rotateCounterClockwise();
					Result result = doDecode(rotatedImage, hints);
					// Record that we found it rotated 90 degrees CCW / 270 degrees CW
					Hashtable metadata = result.getResultMetadata();
					int orientation = 270;
					if (metadata != null && metadata.containsKey(ResultMetadataType.ORIENTATION)) {
						// But if we found it reversed in doDecode(), add in that result here:
						orientation = (orientation +
									   ((Integer) metadata.get(ResultMetadataType.ORIENTATION)).intValue()) % 360;
					}
					result.putMetadata(ResultMetadataType.ORIENTATION, new Integer(orientation));
					// Update result points
					ResultPoint[] points = result.getResultPoints();
					int height = rotatedImage.getHeight();
					for (int i = 0; i < points.length; i++) {
						points[i] = new ResultPoint(height - points[i].getY() - 1, points[i].getX());
					}
					return result;
					*/
				} else {
					throw re;
				}
			}
		}
		
		Ref<Result> OneDReader::doDecode(Ref<BinaryBitmap> image){
			int width = image->getWidth();
			int height = image->getHeight();
			Ref<BitArray> row(new BitArray(width));
//			BitArray row = new BitArray(width);
			
			int middle = height >> 1;
			bool tryHarder = true;//hints != null && hints.containsKey(DecodeHintType.TRY_HARDER);
			int rowStep = (int)fmax(1, height >> (tryHarder ? 7 : 4));
			int maxLines;
			if (tryHarder) {
				maxLines = height; // Look at the whole image, not just the center
			} else {
				maxLines = 9; // Nine rows spaced 1/16 apart is roughly the middle half of the image
			}
			
			for (int x = 0; x < maxLines; x++) {
				
				// Scanning from the middle out. Determine which row we're looking at next:
				int rowStepsAboveOrBelow = (x + 1) >> 1;
				bool isAbove = (x & 0x01) == 0; // i.e. is x even?
				int rowNumber = middle + rowStep * (isAbove ? rowStepsAboveOrBelow : -rowStepsAboveOrBelow);
				if (rowNumber < 0 || rowNumber >= height) {
					// Oops, if we run off the top or bottom, stop
					break;
				}
					
				// Estimate black point for this row and load it:
				try {
					row = image->getBlackRow(rowNumber, row);
				}catch (ReaderException re) {
					continue;
				}
				
				// While we have the image data in a BitArray, it's fairly cheap to reverse it in place to
				// handle decoding upside down barcodes.
				for (int attempt = 0; attempt < 2; attempt++) {
					if (attempt == 1) { // trying again?
						row->reverse(); // reverse the row and continue
					}
					try {
						// Look for a barcode
						Ref<Result> result = decodeRow(rowNumber, row);
						// We found our barcode
						if (attempt == 1) {
							//						// But it was upside down, so note that
							//						result.putMetadata(ResultMetadataType.ORIENTATION, new Integer(180));
							//						// And remember to flip the result points horizontally.
							//						ResultPoint[] points = result.getResultPoints();
							//						points[0] = new ResultPoint(width - points[0].getX() - 1, points[0].getY());
							//						points[1] = new ResultPoint(width - points[1].getX() - 1, points[1].getY());
						}
						return result;
					} catch (ReaderException re) {
						// continue -- just couldn't decode this row
					}
				}
			}
			throw ReaderException("");
		}
		
		int OneDReader::patternMatchVariance(int counters[], int countersSize, const int pattern[], int maxIndividualVariance) {
			int numCounters = countersSize;
			int total = 0;
			int patternLength = 0;
			for (int i = 0; i < numCounters; i++) {
				total += counters[i];
				patternLength += pattern[i];
			}
			if (total < patternLength) {
				// If we don't even have one pixel per unit of bar width, assume this is too small
				// to reliably match, so fail:
				return INT_MAX;
			}
			// We're going to fake floating-point math in integers. We just need to use more bits.
			// Scale up patternLength so that intermediate values below like scaledCounter will have
			// more "significant digits"
			int unitBarWidth = (total << INTEGER_MATH_SHIFT) / patternLength;
			maxIndividualVariance = (maxIndividualVariance * unitBarWidth) >> INTEGER_MATH_SHIFT;
			
			int totalVariance = 0;
			for (int x = 0; x < numCounters; x++) {
				int counter = counters[x] << INTEGER_MATH_SHIFT;
				int scaledPattern = pattern[x] * unitBarWidth;
				int variance = counter > scaledPattern ? counter - scaledPattern : scaledPattern - counter;
				if (variance > maxIndividualVariance) {
					return INT_MAX;
				}
				totalVariance += variance;
			}
			return totalVariance / total;
		}
		
		void OneDReader::recordPattern(Ref<BitArray> row, int start, int counters[], int countersCount){
			int numCounters = countersCount;//sizeof(counters) / sizeof(int);
			for (int i = 0; i < numCounters; i++) {
				counters[i] = 0;
			}
			int end = row->getSize();
			if (start >= end) {
				throw ReaderException("recordPattern: start >= end");
			}
			bool isWhite = !row->get(start);
			int counterPosition = 0;
			int i = start;
			while (i < end) {
				bool pixel = row->get(i);
				if (pixel ^ isWhite) { // that is, exactly one is true
					counters[counterPosition]++;
				} else {
					counterPosition++;
					if (counterPosition == numCounters) {
						break;
					} else {
						counters[counterPosition] = 1;
						isWhite ^= true; // isWhite = !isWhite;
					}
				}
				i++;
			}
			// If we read fully the last section of pixels and filled up our counters -- or filled
			// the last counter but ran off the side of the image, OK. Otherwise, a problem.
			if (!(counterPosition == numCounters || (counterPosition == numCounters - 1 && i == end))) {
				throw ReaderException("recordPattern");
			}
		}
		
		OneDReader::~OneDReader() {
		}
	}
}