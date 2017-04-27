/**
 * Copyright 2016 hbz NRW (http://www.hbz-nrw.de/)
 * 
 * This file is part of regal-drupal.
 * 
 * regal-drupal is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 * 
 * regal-drupal is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * regal-drupal. If not, see <http://www.gnu.org/licenses/>.
 */

(function($) {
	window.addEventListener("message", handleMessage, false);
	Drupal.zettel = {
		useZettel : function useZettel(bundle, entity, context) {
			if (!isEmpty($('.tabs', context))) {
				loadZettel(bundle, entity, context);
			} else {
				var zettel_form = '<div id="successBox" class="success"></div>'
						+ '<div id="warningBox" class="warning"></div>'
						+ '<iframe class="'+bundle+'"name="'+Date.now()+'" src="'
						+ Drupal.settings.edoweb.zettelServiceUrl
						+ '/forms'
						+ '?id=katalog:'
						+ bundle
						+ '&format=xml'
						+ '&documentId=_:foo'
						+ '&topicId='
						+ Drupal.settings.baseUrl
						+ '/resource/add/'
						+ bundle
						+ '"'
						+ ' width="800px" style="border: none;" id="iFrame">'
						+ '<p>iframes are not supported by your browser.</p></iframe>';
				$('.region.region-content').html(zettel_form);
			}
		}
	}
	function loadZettel(bundle, entity, context) {
		console.log(bundle);
		var rid = $(entity).attr("resource");
		var url = Drupal.settings.edoweb.zettelServiceUrl + '/forms'
				+ '?id=katalog:'+bundle + '&format=xml' + '&documentId=' + rid
				+ '&topicId=' + Drupal.settings.baseUrl + '/resource/' + rid
				+ '/edit';
		var rdfBox = '<div id="rdfBox" class="data" style="display:none;"></div>';
		var zettel_form = '<div id="successBox" class="success"></div>'
				+ '<div id="warningBox" class="warning"></div>'
				+ '<iframe class="'+bundle+'" name="'+Date.now()+'" src="' + url + '"'
		                + ' width="800px" style="border: none;" id="iFrame">'
				+ '<p>iframes are not supported by your browser.</p></iframe>';

		var rdf = getRdfFromApi(entity);
		$('.region.region-content').html(rdfBox);
		$('#rdfBox').text(rdf);
		$('.region.region-content').append(zettel_form);
	}
	function onSuccess(postdata) {
		jQuery('#successBox').html(postdata);
		jQuery('#successBox').css('visibility', 'visible');
		jQuery('#warningBox').css('visibility', 'hidden');
		$.blockUI(Drupal.edoweb.blockUIMessage);
		$('button.edoweb.edit.action').hide();
		var url = Drupal.settings.basePath + Drupal.settings.actionPath;
		
		var bundle = postdata.formType;
		$
				.ajax({
					type : 'POST',
					url : url,
					data : decodeURI(postdata),
					contentType : "text/xml;charset=utf-8",
					success : function(data, textStatus, jqXHR) {
						
						var resource_uri = jqXHR
								.getResponseHeader('X-Edoweb-Entity');

						var href = Drupal.settings.basePath + 'resource/'
								+ resource_uri;
						// Newly created resources are placed into the clipboard
						// and a real redirect is triggered.
						if (true) {
							entity_load_json('edoweb_basic', resource_uri).onload = function() {

								if (bundle == 'monograph'
										|| bundle == 'journal'
										|| bundle == 'proceeding'
										|| bundle == 'researchData'
										|| bundle == 'article') {
									window.location = href;
								} else {
									localStorage.setItem('cut_entity',
											this.responseText);
									history.pushState({
										tree : true
									}, null, href);
									Drupal.edoweb.navigateTo(href);
								}
								$.unblockUI();
							};
						} else {
							window.location = href;
						}

					},
					error : function(data, textStatus, jqXHR) {
						$.unblockUI();
					}
				});
	}
	function onFail(data) {
		jQuery('#warningBox').html(data);
		jQuery('#warningBox').css('visibility', 'visible');
		jQuery('#successBox').css('visibility', 'hidden');
	}
	function getMessage(e) {
		var obj = JSON.parse(e);
		if (obj.code == 200) {
			onSuccess(obj.message);
		} else {
			onFail(JSON.stringify(obj.message));
		}
	}
	
	function handleMessage(e) {
		if (e.data.action == 'establishConnection') {
			var topicId = e.data.topicId;
			var bundle = e.data.formType;
			var documentId = e.data.documentId;
			var iframe = document.getElementById("iFrame");
			var target = iframe.contentWindow || iframe;
			var rdf = $('#rdfBox').text();
			if (typeof rdf != "undefined") {
				target.postMessage({
					'queryParam' : 'id='+bundle+'&format=xml&topicId='
							+ topicId + '&documentId=' + documentId,
					'message' : rdf,
					'action'  : 'postDataToZettel'
				}, "*");
			}
		} else if (e.data.action == 'resize') {
			var targetHeight = e.data.message;
			jQuery('#iFrame').height(targetHeight);
		} else if (e.data.action == 'postData') {
			getMessage(decodeURI(e.data.message));
		}
	}

	function getRdfFromApi(entity) {
		return encodeURI(Drupal.settings.rdf);
	}

	function isEmpty(el) {
		return !$.trim(el.html());
	}

})(jQuery);
